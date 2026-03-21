// ═══════════════════════════════════════════════════════════════════════════
// Simulador Orbital — Tierra + Satelite en 3D
// Mecanica Kepleriana + OpenGL 3.3 + Dear ImGui + Textura
// ═══════════════════════════════════════════════════════════════════════════

#define _USE_MATH_DEFINES
#include <cmath>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <vector>
#include <string>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>

// ─── Constantes fisicas ──────────────────────────────────────────────────
namespace phys {
    constexpr double G       = 6.67430e-11;
    constexpr double M_EARTH = 5.972e24;
    constexpr double R_EARTH = 6.371e6;
    constexpr double MU      = G * M_EARTH;
}

// ─── Estado global ───────────────────────────────────────────────────────
static int   WIN_W = 1280, WIN_H = 720;
static float cam_distance = 35.0f;
static float cam_yaw   = -45.0f;
static float cam_pitch =  25.0f;
static bool  mouse_dragging = false;
static double last_mx = 0, last_my = 0;
static float scroll_accum = 0.0f;
static float time_warp = 100.0f;

// ═══════════════════════════════════════════════════════════════════════════
//  SHADERS
// ═══════════════════════════════════════════════════════════════════════════

static const char* SPHERE_VERT = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

out vec3 vNormal;
out vec3 vFragPos;
out vec2 vTexCoord;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vFragPos  = worldPos.xyz;
    vNormal   = mat3(transpose(inverse(uModel))) * aNormal;
    vTexCoord = aTexCoord;
    gl_Position = uProjection * uView * worldPos;
}
)glsl";

static const char* SPHERE_FRAG = R"glsl(
#version 330 core
in vec3 vNormal;
in vec3 vFragPos;
in vec2 vTexCoord;

out vec4 FragColor;

uniform vec3  uColor;
uniform vec3  uLightDir;
uniform vec3  uViewPos;
uniform float uAmbient;
uniform sampler2D uTexture;
uniform int   uUseTexture;

void main() {
    vec3 baseColor = (uUseTexture == 1) ? texture(uTexture, vTexCoord).rgb : uColor;

    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightDir);
    float diff = max(dot(N, L), 0.0);

    vec3 V = normalize(uViewPos - vFragPos);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 32.0);

    vec3 result = (uAmbient + diff * 0.7 + spec * 0.3) * baseColor;
    FragColor = vec4(result, 1.0);
}
)glsl";

static const char* LINE_VERT = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMVP;
void main() { gl_Position = uMVP * vec4(aPos, 1.0); }
)glsl";

static const char* LINE_FRAG = R"glsl(
#version 330 core
uniform vec3 uColor;
out vec4 FragColor;
void main() { FragColor = vec4(uColor, 1.0); }
)glsl";

// ═══════════════════════════════════════════════════════════════════════════
//  UTILIDADES GL
// ═══════════════════════════════════════════════════════════════════════════

GLuint compileShader(const char* src, GLenum type) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    int ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char l[512]; glGetShaderInfoLog(s,512,nullptr,l); std::cerr<<"Shader: "<<l<<"\n"; }
    return s;
}

GLuint createProgram(const char* v, const char* f) {
    GLuint vs = compileShader(v, GL_VERTEX_SHADER);
    GLuint fs = compileShader(f, GL_FRAGMENT_SHADER);
    GLuint p  = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs);
    glLinkProgram(p);
    int ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char l[512]; glGetProgramInfoLog(p,512,nullptr,l); std::cerr<<"Link: "<<l<<"\n"; }
    glDeleteShader(vs); glDeleteShader(fs);
    return p;
}

// ═══════════════════════════════════════════════════════════════════════════
//  ESFERA CON UV
// ═══════════════════════════════════════════════════════════════════════════

struct SphereMesh { GLuint vao=0, vbo=0, ebo=0; int indexCount=0; };

SphereMesh createSphere(float radius, int sectors, int stacks) {
    std::vector<float> verts;
    std::vector<unsigned int> idx;

    for (int i = 0; i <= stacks; ++i) {
        float phi = glm::pi<float>() * (float)i / (float)stacks;
        float v   = (float)i / (float)stacks;
        for (int j = 0; j <= sectors; ++j) {
            float theta = 2.0f * glm::pi<float>() * (float)j / (float)sectors;
            float u     = 1.0f - (float)j / (float)sectors;

            float x = std::sin(phi) * std::cos(theta);
            float y = std::cos(phi);
            float z = std::sin(phi) * std::sin(theta);

            verts.push_back(x * radius); verts.push_back(y * radius); verts.push_back(z * radius);
            verts.push_back(x); verts.push_back(y); verts.push_back(z);
            verts.push_back(u); verts.push_back(v);
        }
    }
    for (int i = 0; i < stacks; ++i)
        for (int j = 0; j < sectors; ++j) {
            int a = i*(sectors+1)+j, b = a+sectors+1;
            idx.push_back(a); idx.push_back(b);   idx.push_back(a+1);
            idx.push_back(a+1); idx.push_back(b); idx.push_back(b+1);
        }

    SphereMesh m; m.indexCount = (int)idx.size();
    glGenVertexArrays(1,&m.vao); glGenBuffers(1,&m.vbo); glGenBuffers(1,&m.ebo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(float), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size()*sizeof(unsigned), idx.data(), GL_STATIC_DRAW);
    // pos
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // normal
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    // uv
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)(6*sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
    return m;
}

// ═══════════════════════════════════════════════════════════════════════════
//  LINEAS
// ═══════════════════════════════════════════════════════════════════════════

struct LineMesh { GLuint vao=0, vbo=0; int vertexCount=0; };

void deleteLineMesh(LineMesh& m) {
    if (m.vao) glDeleteVertexArrays(1,&m.vao);
    if (m.vbo) glDeleteBuffers(1,&m.vbo);
    m = {};
}

LineMesh createLineMesh(const std::vector<float>& verts, int count) {
    LineMesh m; m.vertexCount = count;
    glGenVertexArrays(1,&m.vao); glGenBuffers(1,&m.vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(float), verts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,3*sizeof(float),(void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    return m;
}

LineMesh createAxes(float len) {
    std::vector<float> v = { 0,0,0, len,0,0,  0,0,0, 0,len,0,  0,0,0, 0,0,len };
    return createLineMesh(v, 6);
}

// ═══════════════════════════════════════════════════════════════════════════
//  MECANICA ORBITAL
// ═══════════════════════════════════════════════════════════════════════════

struct OrbitalElements {
    double a, e, i, raan, omega, M0;
};

class Orbit {
public:
    OrbitalElements el;
    double time = 0.0;

    Orbit(const OrbitalElements& e) : el(e) {}

    double meanMotion() const { return std::sqrt(phys::MU / (el.a*el.a*el.a)); }
    double period()     const { return 2.0 * M_PI / meanMotion(); }

    double solveKepler(double M) const {
        double E = M;
        for (int it = 0; it < 50; ++it) {
            double dE = (E - el.e*std::sin(E) - M) / (1.0 - el.e*std::cos(E));
            E -= dE;
            if (std::abs(dE) < 1e-12) break;
        }
        return E;
    }

    double trueAnomaly(double E) const {
        return 2.0 * std::atan2(std::sqrt(1.0+el.e)*std::sin(E/2.0),
                                std::sqrt(1.0-el.e)*std::cos(E/2.0));
    }

    glm::dvec3 positionAt(double t) const {
        double n = meanMotion();
        double M = el.M0 + n*t;
        M = std::fmod(M, 2.0*M_PI);
        double E  = solveKepler(M);
        double nu = trueAnomaly(E);
        double r  = el.a * (1.0 - el.e*std::cos(E));

        double xo = r*std::cos(nu), yo = r*std::sin(nu);
        double ci=std::cos(el.i),   si=std::sin(el.i);
        double co=std::cos(el.raan),so=std::sin(el.raan);
        double cw=std::cos(el.omega),sw=std::sin(el.omega);

        double x = (co*cw-so*sw*ci)*xo + (-co*sw-so*cw*ci)*yo;
        double y = (so*cw+co*sw*ci)*xo + (-so*sw+co*cw*ci)*yo;
        double z = (sw*si)*xo           + (cw*si)*yo;
        return glm::dvec3(x, z, y);
    }

    glm::vec3 posScaled(double t, float er) const {
        return glm::vec3(positionAt(t) * ((double)er / phys::R_EARTH));
    }

    void update(double dt) { time += dt; }
};

LineMesh createOrbitPath(const Orbit& orb, float er, int seg = 500) {
    std::vector<float> v;
    double T = orb.period();
    for (int i = 0; i <= seg; ++i) {
        glm::vec3 p = orb.posScaled(T*(double)i/(double)seg, er);
        v.push_back(p.x); v.push_back(p.y); v.push_back(p.z);
    }
    return createLineMesh(v, seg+1);
}

// ═══════════════════════════════════════════════════════════════════════════
//  TEXTURA PROCEDIMENTAL DE LA TIERRA (fallback)
// ═══════════════════════════════════════════════════════════════════════════

static bool inEllipse(float lat, float lon, float clat, float clon, float rlat, float rlon) {
    float dl = lat - clat;
    float dn = lon - clon;
    if (dn >  180.0f) dn -= 360.0f;
    if (dn < -180.0f) dn += 360.0f;
    return (dl*dl)/(rlat*rlat) + (dn*dn)/(rlon*rlon) <= 1.0f;
}

static bool isLand(float lat, float lon) {
    // Africa
    if (inEllipse(lat, lon, 5, 22, 33, 25))   return true;
    if (inEllipse(lat, lon, 32, 2, 7, 12))    return true;
    // Europe
    if (inEllipse(lat, lon, 50, 15, 18, 28))  return true;
    if (inEllipse(lat, lon, 54, -3, 5, 4))    return true;
    // Asia
    if (inEllipse(lat, lon, 50, 90, 30, 60))  return true;
    if (inEllipse(lat, lon, 20, 78, 15, 12))  return true;
    if (inEllipse(lat, lon, 12, 105, 15, 15)) return true;
    if (inEllipse(lat, lon, 36, 138, 8, 4))   return true;
    // N. America
    if (inEllipse(lat, lon, 50, -100, 25, 35))  return true;
    if (inEllipse(lat, lon, 18, -92, 10, 10))   return true;
    // S. America
    if (inEllipse(lat, lon, -15, -55, 30, 18))  return true;
    // Australia
    if (inEllipse(lat, lon, -25, 135, 15, 20))  return true;
    // Greenland
    if (inEllipse(lat, lon, 72, -42, 10, 12))   return true;
    // Antarctica
    if (lat < -68.0f) return true;
    return false;
}

GLuint createProceduralEarthTexture() {
    const int W = 1024, H = 512;
    std::vector<unsigned char> data(W * H * 3);

    for (int y = 0; y < H; ++y) {
        float lat = 90.0f - (float)y / H * 180.0f;
        float latFactor = std::cos(glm::radians(lat));
        for (int x = 0; x < W; ++x) {
            float lon = (float)x / W * 360.0f - 180.0f;
            int idx = (y * W + x) * 3;

            float r, g, b;
            if (isLand(lat, lon)) {
                float absLat = std::abs(lat);
                if (absLat > 65.0f)       { r=0.90f; g=0.92f; b=0.95f; } // nieve
                else if (absLat > 50.0f)  { r=0.35f; g=0.50f; b=0.25f; } // bosque boreal
                else if (absLat > 23.5f)  { r=0.45f; g=0.55f; b=0.28f; } // temperado
                else                       { r=0.20f; g=0.60f; b=0.15f; } // tropical
            } else {
                r = 0.04f + 0.08f*latFactor;
                g = 0.12f + 0.15f*latFactor;
                b = 0.35f + 0.35f*latFactor;
            }

            // Grid cada 30 grados
            float latMod = std::fmod(std::abs(lat)+0.5f, 30.0f);
            float lonMod = std::fmod(std::abs(lon)+0.5f, 30.0f);
            if (latMod < 0.8f || lonMod < 0.8f) { r+=0.08f; g+=0.08f; b+=0.08f; }
            // Ecuator
            if (std::abs(lat) < 1.0f) { r = std::min(r+0.15f,1.0f); g = std::min(g+0.12f,1.0f); }

            data[idx]   = (unsigned char)(std::min(r,1.0f)*255);
            data[idx+1] = (unsigned char)(std::min(g,1.0f)*255);
            data[idx+2] = (unsigned char)(std::min(b,1.0f)*255);
        }
    }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, W, H, 0, GL_RGB, GL_UNSIGNED_BYTE, data.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

GLuint loadTextureFromFile(const char* path) {
    int w, h, ch;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* data = stbi_load(path, &w, &h, &ch, 3);
    if (!data) return 0;

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    stbi_image_free(data);
    return tex;
}

// ═══════════════════════════════════════════════════════════════════════════
//  CALLBACKS
// ═══════════════════════════════════════════════════════════════════════════

void framebufferCB(GLFWwindow*, int w, int h) { WIN_W=w; WIN_H=h; glViewport(0,0,w,h); }

void scrollCB(GLFWwindow*, double, double yoff) { scroll_accum += (float)yoff; }

// ═══════════════════════════════════════════════════════════════════════════
//  PRESETS ORBITALES
// ═══════════════════════════════════════════════════════════════════════════

struct OrbitPreset { const char* name; float alt_km; float ecc; float inc_deg; float raan_deg; float omega_deg; };

static const OrbitPreset PRESETS[] = {
    { "ISS (LEO)",          408.0f,    0.0007f, 51.6f,  30.0f,  0.0f },
    { "GEO",                35786.0f,  0.0f,    0.0f,   0.0f,   0.0f },
    { "Molniya",            20229.0f,  0.74f,   63.4f,  -70.0f, 270.0f },
    { "Sun-sync (SSO)",     600.0f,    0.001f,  97.8f,  45.0f,  0.0f },
    { "GPS (MEO)",          20200.0f,  0.0f,    55.0f,  0.0f,   0.0f },
    { "Hubble",             547.0f,    0.0003f, 28.5f,  0.0f,   0.0f },
};
static const int NUM_PRESETS = sizeof(PRESETS)/sizeof(PRESETS[0]);

// ═══════════════════════════════════════════════════════════════════════════
//  MAIN
// ═══════════════════════════════════════════════════════════════════════════

int main() {
    if (!glfwInit()) { std::cerr << "GLFW init failed\n"; return -1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    GLFWwindow* window = glfwCreateWindow(WIN_W, WIN_H,
        "Simulador Orbital", nullptr, nullptr);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);

    glfwSetFramebufferSizeCallback(window, framebufferCB);
    glfwSetScrollCallback(window, scrollCB);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "GLAD failed\n"; return -1;
    }
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);

    // ── ImGui ──────────────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    // Estilo mas oscuro y compacto
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding   = 6.0f;
    style.FrameRounding    = 4.0f;
    style.GrabRounding     = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FramePadding     = ImVec2(6, 4);
    style.ItemSpacing      = ImVec2(8, 4);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // ── Shaders ────────────────────────────────────────────────────────
    GLuint progSphere = createProgram(SPHERE_VERT, SPHERE_FRAG);
    GLuint progLine   = createProgram(LINE_VERT, LINE_FRAG);

    // ── Geometria ──────────────────────────────────────────────────────
    const float EARTH_R = 8.0f;
    SphereMesh earthMesh = createSphere(EARTH_R, 64, 32);
    // Satelite: cubo pequeno
    struct CubeMesh { GLuint vao=0, vbo=0; int vertexCount=0; };
    CubeMesh satMesh;
    {
        float s = 0.15f; // mitad del lado
        float cube[] = {
            // pos              normal
            -s,-s,-s,  0,0,-1,  s,-s,-s, 0,0,-1,  s, s,-s, 0,0,-1,
            -s,-s,-s,  0,0,-1,  s, s,-s, 0,0,-1, -s, s,-s, 0,0,-1,
            -s,-s, s,  0,0, 1,  s, s, s, 0,0, 1,  s,-s, s, 0,0, 1,
            -s,-s, s,  0,0, 1, -s, s, s, 0,0, 1,  s, s, s, 0,0, 1,
            -s, s,-s,  0,1, 0,  s, s,-s, 0,1, 0,  s, s, s, 0,1, 0,
            -s, s,-s,  0,1, 0,  s, s, s, 0,1, 0, -s, s, s, 0,1, 0,
            -s,-s,-s,  0,-1,0,  s,-s, s, 0,-1,0,  s,-s,-s, 0,-1,0,
            -s,-s,-s,  0,-1,0, -s,-s, s, 0,-1,0,  s,-s, s, 0,-1,0,
             s,-s,-s,  1,0, 0,  s,-s, s, 1,0, 0,  s, s, s, 1,0, 0,
             s,-s,-s,  1,0, 0,  s, s, s, 1,0, 0,  s, s,-s, 1,0, 0,
            -s,-s,-s, -1,0, 0, -s, s, s,-1,0, 0, -s,-s, s,-1,0, 0,
            -s,-s,-s, -1,0, 0, -s, s,-s,-1,0, 0, -s, s, s,-1,0, 0,
        };
        satMesh.vertexCount = 36;
        glGenVertexArrays(1,&satMesh.vao); glGenBuffers(1,&satMesh.vbo);
        glBindVertexArray(satMesh.vao);
        glBindBuffer(GL_ARRAY_BUFFER, satMesh.vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(cube), cube, GL_STATIC_DRAW);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,6*sizeof(float),(void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,6*sizeof(float),(void*)(3*sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
    }

    // ── Textura de la Tierra ───────────────────────────────────────────
    GLuint earthTex = loadTextureFromFile("earth_texture.jpg");
    bool hasRealTexture = (earthTex != 0);
    if (!earthTex) earthTex = loadTextureFromFile("earth_texture.png");
    if (!earthTex) {
        std::cout << "[INFO] No se encontro earth_texture.jpg/png, usando textura procedimental.\n";
        std::cout << "       Descarga una textura de: https://visibleearth.nasa.gov/images/73909\n";
        std::cout << "       y guardala como 'earth_texture.jpg' junto a main.cpp\n\n";
        earthTex = createProceduralEarthTexture();
    } else {
        hasRealTexture = true;
    }

    // ── Orbita ─────────────────────────────────────────────────────────
    OrbitalElements elems;
    elems.a     = phys::R_EARTH + 408000.0;
    elems.e     = 0.0007;
    elems.i     = glm::radians(51.6);
    elems.raan  = glm::radians(30.0);
    elems.omega = 0.0;
    elems.M0    = 0.0;

    Orbit orbit(elems);
    LineMesh orbitPath = createOrbitPath(orbit, EARTH_R);
    LineMesh axes      = createAxes(EARTH_R * 2.0f);

    // Parametros editables (para ImGui)
    float ui_alt_km   = 408.0f;
    float ui_ecc      = 0.0007f;
    float ui_inc_deg  = 51.6f;
    float ui_raan_deg = 30.0f;
    float ui_omg_deg  = 0.0f;

    // ── Estrellas ──────────────────────────────────────────────────────
    std::vector<float> starVerts;
    srand(42);
    for (int i = 0; i < 3000; ++i) {
        float th = ((float)rand()/RAND_MAX)*2.0f*glm::pi<float>();
        float ph = std::acos(2.0f*((float)rand()/RAND_MAX)-1.0f);
        float r  = 150.0f;
        starVerts.push_back(r*std::sin(ph)*std::cos(th));
        starVerts.push_back(r*std::cos(ph));
        starVerts.push_back(r*std::sin(ph)*std::sin(th));
    }
    GLuint starVAO, starVBO;
    glGenVertexArrays(1,&starVAO); glGenBuffers(1,&starVBO);
    glBindVertexArray(starVAO);
    glBindBuffer(GL_ARRAY_BUFFER, starVBO);
    glBufferData(GL_ARRAY_BUFFER, starVerts.size()*sizeof(float), starVerts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,3*sizeof(float),(void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // ── Trail ──────────────────────────────────────────────────────────
    const int TRAIL_MAX = 800;
    std::vector<float> trailVerts(TRAIL_MAX*3, 0.0f);
    int trailCount = 0; float trailTimer = 0.0f;
    GLuint trailVAO, trailVBO;
    glGenVertexArrays(1,&trailVAO); glGenBuffers(1,&trailVBO);
    glBindVertexArray(trailVAO);
    glBindBuffer(GL_ARRAY_BUFFER, trailVBO);
    glBufferData(GL_ARRAY_BUFFER, trailVerts.size()*sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,3*sizeof(float),(void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // ── Variables de estado ────────────────────────────────────────────
    double lastTime = glfwGetTime();
    bool paused = false;
    float earth_rotation = 0.0f;
    bool show_orbit = true, show_axes = true, show_stars = true, show_trail = true;
    bool use_texture = true;

    std::cout << "======================================================\n";
    std::cout << "  Simulador Orbital - Tierra + Satelite\n";
    std::cout << "  Controles en el panel ImGui (izquierda)\n";
    std::cout << "  Arrastra el raton para rotar | Scroll para zoom\n";
    std::cout << "======================================================\n\n";

    // ── Loop principal ────────────────────────────────────────────────
    while (!glfwWindowShouldClose(window)) {
        double now = glfwGetTime();
        float dt = (float)(now - lastTime);
        lastTime = now;

        glfwPollEvents();

        // ── ImGui: nuevo frame ─────────────────────────────────────
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // ── Camara (solo si ImGui no quiere el mouse) ──────────────
        if (!io.WantCaptureMouse) {
            // Scroll → zoom
            if (scroll_accum != 0.0f) {
                cam_distance -= scroll_accum * 2.0f;
                cam_distance = glm::clamp(cam_distance, 5.0f, 200.0f);
            }
            // Drag → rotar
            double mx, my;
            glfwGetCursorPos(window, &mx, &my);
            bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            if (leftDown) {
                if (mouse_dragging) {
                    cam_yaw   += (float)(mx - last_mx) * 0.3f;
                    cam_pitch += (float)(my - last_my) * 0.3f;
                    cam_pitch  = glm::clamp(cam_pitch, -89.0f, 89.0f);
                }
                mouse_dragging = true;
            } else {
                mouse_dragging = false;
            }
            last_mx = mx; last_my = my;
        } else {
            mouse_dragging = false;
        }
        scroll_accum = 0.0f;

        // ── Teclado (controles rapidos) ────────────────────────────
        if (!io.WantCaptureKeyboard) {
            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
                glfwSetWindowShouldClose(window, true);

            static bool sp_was = false;
            bool sp_is = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
            if (sp_is && !sp_was) paused = !paused;
            sp_was = sp_is;
        }

        // ── Simulacion ─────────────────────────────────────────────
        float sim_dt = paused ? 0.0f : dt * time_warp;
        orbit.update(sim_dt);
        earth_rotation += sim_dt * (float)(2.0 * M_PI / 86400.0); // 1 rotacion = 24h
        glm::vec3 satPos = orbit.posScaled(orbit.time, EARTH_R);

        // Trail
        trailTimer += sim_dt;
        if (trailTimer > 5.0f && !paused && trailCount < TRAIL_MAX) {
            trailTimer = 0.0f;
            trailVerts[trailCount*3+0] = satPos.x;
            trailVerts[trailCount*3+1] = satPos.y;
            trailVerts[trailCount*3+2] = satPos.z;
            trailCount++;
            glBindBuffer(GL_ARRAY_BUFFER, trailVBO);
            glBufferSubData(GL_ARRAY_BUFFER, 0, trailCount*3*sizeof(float), trailVerts.data());
        }

        // Datos orbitales actuales
        double posLen = glm::length(orbit.positionAt(orbit.time));
        double alt_km = (posLen - phys::R_EARTH) / 1000.0;
        double vel_kms = std::sqrt(phys::MU * (2.0/posLen - 1.0/orbit.el.a)) / 1000.0;
        double period_min = orbit.period() / 60.0;

        // ═══════════════════════════════════════════════════════════
        //  PANEL IMGUI
        // ═══════════════════════════════════════════════════════════

        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);
        ImGui::Begin("Control de Simulacion");

        // Seccion: Tiempo
        ImGui::SeparatorText("Tiempo");
        ImGui::SliderFloat("Warp", &time_warp, 1.0f, 10000.0f, "x%.0f", ImGuiSliderFlags_Logarithmic);
        if (ImGui::Button(paused ? "  Reanudar  " : "  Pausar  ")) paused = !paused;
        ImGui::SameLine();
        if (ImGui::Button("Reiniciar")) {
            orbit.time = 0.0;
            trailCount = 0;
            earth_rotation = 0.0f;
        }
        double elapsed = orbit.time;
        int hrs = (int)(elapsed / 3600.0);
        int mins = (int)(std::fmod(elapsed, 3600.0) / 60.0);
        int secs = (int)(std::fmod(elapsed, 60.0));
        ImGui::Text("Tiempo: %02d:%02d:%02d", hrs, mins, secs);

        // Seccion: Informacion orbital
        ImGui::SeparatorText("Info Orbital");
        ImGui::Text("Altitud:  %.1f km", alt_km);
        ImGui::Text("Velocidad: %.3f km/s", vel_kms);
        ImGui::Text("Periodo:  %.1f min", period_min);

        // Seccion: Parametros orbitales
        ImGui::SeparatorText("Parametros Orbitales");
        ImGui::SliderFloat("Altitud (km)", &ui_alt_km, 200.0f, 42000.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Excentricidad", &ui_ecc, 0.0f, 0.95f, "%.4f");
        ImGui::SliderFloat("Inclinacion", &ui_inc_deg, 0.0f, 180.0f, "%.1f deg");
        ImGui::SliderFloat("RAAN", &ui_raan_deg, -180.0f, 360.0f, "%.1f deg");
        ImGui::SliderFloat("Arg Periapsis", &ui_omg_deg, 0.0f, 360.0f, "%.1f deg");

        if (ImGui::Button("Aplicar Cambios")) {
            orbit.el.a     = phys::R_EARTH + (double)ui_alt_km * 1000.0;
            orbit.el.e     = ui_ecc;
            orbit.el.i     = glm::radians((double)ui_inc_deg);
            orbit.el.raan  = glm::radians((double)ui_raan_deg);
            orbit.el.omega = glm::radians((double)ui_omg_deg);
            orbit.time = 0.0;
            trailCount = 0;
            deleteLineMesh(orbitPath);
            orbitPath = createOrbitPath(orbit, EARTH_R);
        }

        // Presets
        ImGui::SeparatorText("Presets");
        for (int i = 0; i < NUM_PRESETS; ++i) {
            if (i > 0 && i % 3 != 0) ImGui::SameLine();
            if (ImGui::Button(PRESETS[i].name)) {
                ui_alt_km   = PRESETS[i].alt_km;
                ui_ecc      = PRESETS[i].ecc;
                ui_inc_deg  = PRESETS[i].inc_deg;
                ui_raan_deg = PRESETS[i].raan_deg;
                ui_omg_deg  = PRESETS[i].omega_deg;

                orbit.el.a     = phys::R_EARTH + (double)ui_alt_km * 1000.0;
                orbit.el.e     = ui_ecc;
                orbit.el.i     = glm::radians((double)ui_inc_deg);
                orbit.el.raan  = glm::radians((double)ui_raan_deg);
                orbit.el.omega = glm::radians((double)ui_omg_deg);
                orbit.time = 0.0;
                trailCount = 0;
                deleteLineMesh(orbitPath);
                orbitPath = createOrbitPath(orbit, EARTH_R);
            }
        }

        // Seccion: Visualizacion
        ImGui::SeparatorText("Visualizacion");
        ImGui::Checkbox("Orbita", &show_orbit);
        ImGui::SameLine(); ImGui::Checkbox("Ejes", &show_axes);
        ImGui::Checkbox("Estrellas", &show_stars);
        ImGui::SameLine(); ImGui::Checkbox("Trail", &show_trail);
        ImGui::Checkbox("Textura Tierra", &use_texture);

        ImGui::End();

        // ═══════════════════════════════════════════════════════════
        //  RENDERIZADO 3D
        // ═══════════════════════════════════════════════════════════

        float cy = glm::radians(cam_yaw), cp = glm::radians(cam_pitch);
        glm::vec3 camPos(cam_distance*std::cos(cp)*std::cos(cy),
                         cam_distance*std::sin(cp),
                         cam_distance*std::cos(cp)*std::sin(cy));
        glm::mat4 view = glm::lookAt(camPos, glm::vec3(0), glm::vec3(0,1,0));
        glm::mat4 proj = glm::perspective(glm::radians(45.0f),
            (float)WIN_W/(float)WIN_H, 0.1f, 500.0f);

        glClearColor(0.02f, 0.02f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::vec3 lightDir = glm::normalize(glm::vec3(1.0f, 0.3f, 0.5f));
        glm::mat4 mvp = proj * view;

        // --- Estrellas ---
        if (show_stars) {
            glUseProgram(progLine);
            glUniformMatrix4fv(glGetUniformLocation(progLine,"uMVP"),1,GL_FALSE,glm::value_ptr(mvp));
            glUniform3f(glGetUniformLocation(progLine,"uColor"),0.8f,0.8f,0.9f);
            glBindVertexArray(starVAO);
            glPointSize(1.5f);
            glDrawArrays(GL_POINTS, 0, 3000);
        }

        // --- Ejes ---
        if (show_axes) {
            glUseProgram(progLine);
            glUniformMatrix4fv(glGetUniformLocation(progLine,"uMVP"),1,GL_FALSE,glm::value_ptr(mvp));
            glLineWidth(1.5f);
            glBindVertexArray(axes.vao);
            glUniform3f(glGetUniformLocation(progLine,"uColor"),0.7f,0.2f,0.2f); glDrawArrays(GL_LINES,0,2);
            glUniform3f(glGetUniformLocation(progLine,"uColor"),0.2f,0.7f,0.2f); glDrawArrays(GL_LINES,2,2);
            glUniform3f(glGetUniformLocation(progLine,"uColor"),0.2f,0.2f,0.7f); glDrawArrays(GL_LINES,4,2);
        }

        // --- Orbita ---
        if (show_orbit) {
            glUseProgram(progLine);
            glUniformMatrix4fv(glGetUniformLocation(progLine,"uMVP"),1,GL_FALSE,glm::value_ptr(mvp));
            glLineWidth(1.0f);
            glBindVertexArray(orbitPath.vao);
            glUniform3f(glGetUniformLocation(progLine,"uColor"),0.3f,0.6f,0.9f);
            glDrawArrays(GL_LINE_STRIP, 0, orbitPath.vertexCount);
        }

        // --- Trail ---
        if (show_trail && trailCount > 1) {
            glUseProgram(progLine);
            glUniformMatrix4fv(glGetUniformLocation(progLine,"uMVP"),1,GL_FALSE,glm::value_ptr(mvp));
            glBindVertexArray(trailVAO);
            glUniform3f(glGetUniformLocation(progLine,"uColor"),1.0f,0.5f,0.1f);
            glDrawArrays(GL_LINE_STRIP, 0, trailCount);
        }

        // --- Tierra ---
        glUseProgram(progSphere);
        glm::mat4 earthModel = glm::rotate(glm::mat4(1.0f), earth_rotation, glm::vec3(0,1,0));
        glUniformMatrix4fv(glGetUniformLocation(progSphere,"uModel"),1,GL_FALSE,glm::value_ptr(earthModel));
        glUniformMatrix4fv(glGetUniformLocation(progSphere,"uView"),1,GL_FALSE,glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(progSphere,"uProjection"),1,GL_FALSE,glm::value_ptr(proj));
        glUniform3fv(glGetUniformLocation(progSphere,"uLightDir"),1,glm::value_ptr(lightDir));
        glUniform3fv(glGetUniformLocation(progSphere,"uViewPos"),1,glm::value_ptr(camPos));
        glUniform3f(glGetUniformLocation(progSphere,"uColor"),0.15f,0.45f,0.75f);
        glUniform1f(glGetUniformLocation(progSphere,"uAmbient"),0.15f);

        if (use_texture && earthTex) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, earthTex);
            glUniform1i(glGetUniformLocation(progSphere,"uTexture"), 0);
            glUniform1i(glGetUniformLocation(progSphere,"uUseTexture"), 1);
        } else {
            glUniform1i(glGetUniformLocation(progSphere,"uUseTexture"), 0);
        }

        glBindVertexArray(earthMesh.vao);
        glDrawElements(GL_TRIANGLES, earthMesh.indexCount, GL_UNSIGNED_INT, 0);

        // --- Satelite ---
        glm::mat4 satModel = glm::translate(glm::mat4(1.0f), satPos);
        glUniformMatrix4fv(glGetUniformLocation(progSphere,"uModel"),1,GL_FALSE,glm::value_ptr(satModel));
        glUniform3f(glGetUniformLocation(progSphere,"uColor"),0.9f,0.85f,0.2f);
        glUniform1f(glGetUniformLocation(progSphere,"uAmbient"),0.4f);
        glUniform1i(glGetUniformLocation(progSphere,"uUseTexture"), 0);
        glBindVertexArray(satMesh.vao);
        glDrawArrays(GL_TRIANGLES, 0, satMesh.vertexCount);

        // --- Titulo ventana ---
        {
            std::ostringstream t;
            t << std::fixed << std::setprecision(1)
              << "Simulador Orbital | Alt: " << alt_km << " km"
              << " | Vel: " << vel_kms << " km/s"
              << " | Warp: x" << time_warp
              << (paused ? " [PAUSADO]" : "");
            glfwSetWindowTitle(window, t.str().c_str());
        }

        // --- Renderizar ImGui ---
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // ── Limpieza ───────────────────────────────────────────────────────
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glDeleteVertexArrays(1,&earthMesh.vao); glDeleteBuffers(1,&earthMesh.vbo); glDeleteBuffers(1,&earthMesh.ebo);
    glDeleteVertexArrays(1,&satMesh.vao);   glDeleteBuffers(1,&satMesh.vbo);
    deleteLineMesh(orbitPath); deleteLineMesh(axes);
    glDeleteVertexArrays(1,&starVAO);  glDeleteBuffers(1,&starVBO);
    glDeleteVertexArrays(1,&trailVAO); glDeleteBuffers(1,&trailVBO);
    if (earthTex) glDeleteTextures(1, &earthTex);
    glDeleteProgram(progSphere); glDeleteProgram(progLine);

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
