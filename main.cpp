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
#include <fstream>
#include <chrono>
#include <ctime>
#include <array>
#include <future>

#include "scenario.h"
#include "orbit.h"

// ─── Estado global ───────────────────────────────────────────────────────
static int   WIN_W = 1280, WIN_H = 720;
static float cam_distance = 35.0f;
static float cam_yaw   = -45.0f;
static float cam_pitch =  25.0f;
static bool  mouse_dragging = false;
static double last_mx = 0, last_my = 0;
static float scroll_accum = 0.0f;
static float time_warp = 100.0f;

static constexpr double SIDEREAL_DAY_SECONDS = 86164.0905;

static double julianDateFromUnix(double unixSeconds) {
    return unixSeconds / 86400.0 + 2440587.5;
}

static double gmstRadiansFromUnix(double unixSeconds) {
    const double jd = julianDateFromUnix(unixSeconds);
    const double d = jd - 2451545.0;
    double gmstDeg = 280.46061837 + 360.98564736629 * d;
    gmstDeg = std::fmod(gmstDeg, 360.0);
    if (gmstDeg < 0.0) gmstDeg += 360.0;
    return glm::radians(gmstDeg);
}

// Approximate solar direction in ECI, then mapped to this renderer axis convention.
static glm::vec3 sunDirectionFromUnix(double unixSeconds) {
    const double jd = julianDateFromUnix(unixSeconds);
    const double n = jd - 2451545.0;

    double L = 280.460 + 0.9856474 * n;
    double g = 357.528 + 0.9856003 * n;
    L = std::fmod(L, 360.0);
    g = std::fmod(g, 360.0);
    if (L < 0.0) L += 360.0;
    if (g < 0.0) g += 360.0;

    const double gRad = glm::radians(g);
    const double lambdaDeg = L + 1.915 * std::sin(gRad) + 0.020 * std::sin(2.0 * gRad);
    const double epsDeg = 23.439 - 0.0000004 * n;

    const double lambda = glm::radians(lambdaDeg);
    const double eps = glm::radians(epsDeg);

    const double xEci = std::cos(lambda);
    const double yEci = std::cos(eps) * std::sin(lambda);
    const double zEci = std::sin(eps) * std::sin(lambda);

    return glm::normalize(glm::vec3((float)xEci, (float)zEci, (float)yEci));
}

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

static void drawMetricCard(const char* id, const char* title, const std::string& value, const ImVec4& accent) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.11f, 0.15f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(accent.x, accent.y, accent.z, 0.45f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::BeginChild(id, ImVec2(0, 62), true);
    ImGui::TextColored(ImVec4(0.78f, 0.86f, 0.94f, 1.0f), "%s", title);
    ImGui::TextColored(accent, "%s", value.c_str());
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

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
//  MAIN
// ═══════════════════════════════════════════════════════════════════════════

int main() {
    if (!glfwInit()) { std::cerr << "GLFW init failed\n"; return -1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    GLFWwindow* window = glfwCreateWindow(WIN_W, WIN_H,
        "Satellite Downlink Simulator", nullptr, nullptr);
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

    // Estilo tecnico moderno
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 10.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 10.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.FramePadding = ImVec2(8, 6);
    style.ItemSpacing = ImVec2(9, 7);
    style.ItemInnerSpacing = ImVec2(6, 5);
    style.WindowPadding = ImVec2(12, 10);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]         = ImVec4(0.05f, 0.07f, 0.10f, 0.97f);
    colors[ImGuiCol_ChildBg]          = ImVec4(0.07f, 0.09f, 0.13f, 0.95f);
    colors[ImGuiCol_PopupBg]          = ImVec4(0.08f, 0.10f, 0.14f, 0.97f);
    colors[ImGuiCol_Border]           = ImVec4(0.22f, 0.30f, 0.38f, 0.55f);
    colors[ImGuiCol_FrameBg]          = ImVec4(0.10f, 0.14f, 0.19f, 0.92f);
    colors[ImGuiCol_FrameBgHovered]   = ImVec4(0.16f, 0.24f, 0.32f, 0.94f);
    colors[ImGuiCol_FrameBgActive]    = ImVec4(0.19f, 0.30f, 0.39f, 0.98f);
    colors[ImGuiCol_Button]           = ImVec4(0.15f, 0.32f, 0.43f, 0.85f);
    colors[ImGuiCol_ButtonHovered]    = ImVec4(0.20f, 0.44f, 0.56f, 0.92f);
    colors[ImGuiCol_ButtonActive]     = ImVec4(0.23f, 0.52f, 0.64f, 0.98f);
    colors[ImGuiCol_Header]           = ImVec4(0.15f, 0.30f, 0.41f, 0.80f);
    colors[ImGuiCol_HeaderHovered]    = ImVec4(0.22f, 0.45f, 0.57f, 0.90f);
    colors[ImGuiCol_HeaderActive]     = ImVec4(0.24f, 0.52f, 0.64f, 0.95f);
    colors[ImGuiCol_Tab]              = ImVec4(0.10f, 0.15f, 0.22f, 0.95f);
    colors[ImGuiCol_TabHovered]       = ImVec4(0.21f, 0.43f, 0.56f, 0.90f);
    colors[ImGuiCol_TabActive]        = ImVec4(0.18f, 0.36f, 0.48f, 1.0f);
    colors[ImGuiCol_TitleBg]          = ImVec4(0.08f, 0.12f, 0.18f, 1.0f);
    colors[ImGuiCol_TitleBgActive]    = ImVec4(0.12f, 0.19f, 0.26f, 1.0f);
    colors[ImGuiCol_TableHeaderBg]    = ImVec4(0.11f, 0.17f, 0.23f, 0.95f);
    colors[ImGuiCol_TableRowBgAlt]    = ImVec4(0.09f, 0.12f, 0.18f, 0.42f);
    colors[ImGuiCol_PlotLines]        = ImVec4(0.32f, 0.77f, 0.90f, 1.0f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.65f, 0.90f, 0.98f, 1.0f);

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
        std::cout << "[INFO] earth_texture.jpg/png not found, using procedural texture.\n";
        std::cout << "       Download one from: https://visibleearth.nasa.gov/images/73909\n";
        std::cout << "       and place it as 'earth_texture.jpg' next to main.cpp\n\n";
        earthTex = createProceduralEarthTexture();
    } else {
        hasRealTexture = true;
    }

    // ── Escenario JSON ─────────────────────────────────────────────────
    ConfigDiagnostics cfgDiag;
    std::vector<SatelliteScenario> satellites = loadSatelliteScenarios("config/satellites.json", cfgDiag);
    int selectedSatIdx = 0;
    SatelliteScenario satScenario = satellites[selectedSatIdx];
    std::vector<AntennaScenario> antennas = loadAntennaScenario("config/antennas.json", cfgDiag);
    SimScenario simScenario = loadSimScenario("config/sim.json", cfgDiag);
    time_warp = simScenario.initial_time_warp;
    double sim_unix = (double)std::time(nullptr);

    // ── Orbita ─────────────────────────────────────────────────────────
    OrbitalElements elems = satScenario.elements;

    auto orbitStartFromUnix = [](const SatelliteScenario& sat, double unixNow) {
        if (sat.propagator == "sgp4_tle" && sat.tle_loaded) {
            return std::max(0.0, unixNow - sat.tle_epoch_unix);
        }
        Orbit tmp(sat.elements);
        const double p = tmp.period();
        if (p <= 0.0) return 0.0;
        double t = std::fmod(std::max(0.0, unixNow), p);
        if (t < 0.0) t += p;
        return t;
    };

    Orbit orbit(elems);
    orbit.setMeanMotionOverride((satScenario.propagator == "sgp4_tle" && satScenario.tle_loaded) ? satScenario.tle_mean_motion_rad_s : 0.0);
    orbit.time = orbitStartFromUnix(satScenario, sim_unix);
    LineMesh orbitPath = createOrbitPath(orbit, EARTH_R);
    LineMesh axes      = createAxes(EARTH_R * 2.0f);

    // Parametros editables (para ImGui)
    float ui_alt_km   = (float)((satScenario.elements.a - phys::R_EARTH) / 1000.0);
    float ui_ecc      = (float)satScenario.elements.e;
    float ui_inc_deg  = (float)glm::degrees(satScenario.elements.i);
    float ui_raan_deg = (float)glm::degrees(satScenario.elements.raan);
    float ui_omg_deg  = (float)glm::degrees(satScenario.elements.omega);

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

    std::vector<float> linkVerts(std::max(1, (int)antennas.size()) * 6, 0.0f);
    GLuint linkVAO, linkVBO;
    glGenVertexArrays(1, &linkVAO); glGenBuffers(1, &linkVBO);
    glBindVertexArray(linkVAO);
    glBindBuffer(GL_ARRAY_BUFFER, linkVBO);
    glBufferData(GL_ARRAY_BUFFER, linkVerts.size() * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // ── Variables de estado ────────────────────────────────────────────
    double lastTime = glfwGetTime();
    bool paused = false;
    float earth_rotation = (float)(gmstRadiansFromUnix(sim_unix));
    bool show_orbit = true, show_axes = true, show_stars = true, show_trail = true;
    bool use_texture = true;
    bool show_antennas = true, show_links = true;
    bool use_shannon_model = simScenario.use_shannon_model;
    bool real_utc_mode = simScenario.real_utc_default;
    float rain_rate_mm_h = simScenario.rain_rate_mm_h;
    float shannon_eff = simScenario.shannon_efficiency;
    float elevation_mask_deg = simScenario.elevation_mask_deg;
    std::vector<LinkTelemetry> linkState(antennas.size());
    std::vector<std::string> eventLog;
    std::vector<RealSatelliteEntry> realSatCatalog;
    int realTopCount = 20;
    int realNoradId = 25544;
    int realCacheTtlHours = 24;
    int realSelectedRow = -1;
    std::string realSatStatus = "Ready to fetch from CelesTrak";
    bool realFetchInProgress = false;
    bool realFetchWasTop = true;
    int realFetchNoradRequested = 0;
    std::future<RealSatelliteFetch> realFetchFuture;
    std::vector<ContactSummary> contacts(antennas.size());
    std::array<float, 240> marginHist{};
    std::array<float, 240> throughputHist{};
    std::vector<std::array<float, 240>> marginHistByAntenna(antennas.size());
    std::vector<std::array<float, 240>> throughputHistByAntenna(antennas.size());
    int histCount = 0;
    int histHead = 0;
    float histTimer = 0.0f;
    int lastSatIdx = selectedSatIdx;
    int selectedAntennaIdx = 0;
    std::string exportStatus;

    auto parseNoradFromLine1 = [](const std::string& line1) -> int {
        if (line1.size() < 7 || line1[0] != '1') return -1;
        try {
            return std::stoi(line1.substr(2, 5));
        } catch (...) {
            return -1;
        }
    };

    auto upsertImportedSatellite = [&](const RealSatelliteEntry& entry) -> int {
        const int incomingNorad = entry.norad_id;
        for (int i = 0; i < (int)satellites.size(); ++i) {
            int existingNorad = parseNoradFromLine1(satellites[i].tle_line1);
            if (incomingNorad > 0 && existingNorad > 0 && incomingNorad == existingNorad) {
                satellites[i] = entry.satellite;
                return i;
            }
        }
        for (int i = 0; i < (int)satellites.size(); ++i) {
            if (satellites[i].name == entry.satellite.name) {
                satellites[i] = entry.satellite;
                return i;
            }
        }
        satellites.push_back(entry.satellite);
        return (int)satellites.size() - 1;
    };

    auto ingestFetchDiagnostics = [&](const RealSatelliteFetch& fetched) {
        for (const std::string& w : fetched.warnings) {
            pushWarn(cfgDiag, "real_sats: " + w);
        }
        for (const std::string& e : fetched.errors) {
            pushErr(cfgDiag, "real_sats: " + e);
        }
    };

    auto finalizeRealFetch = [&](const RealSatelliteFetch& fetched) {
        ingestFetchDiagnostics(fetched);
        if (realFetchWasTop) {
            if (!fetched.satellites.empty()) {
                realSatCatalog = fetched.satellites;
                realSelectedRow = -1;
                std::ostringstream st;
                st << "Fetched " << fetched.satellites.size() << " satellites"
                   << (fetched.used_cache ? " (cache)" : " (network)");
                realSatStatus = st.str();
            } else {
                realSatStatus = "Fetch Top failed. See Diagnostics for details.";
            }
            return;
        }

        if (!fetched.satellites.empty()) {
            const RealSatelliteEntry& s = fetched.satellites.front();
            bool replaced = false;
            for (RealSatelliteEntry& ex : realSatCatalog) {
                if ((s.norad_id > 0 && ex.norad_id == s.norad_id) || ex.satellite.name == s.satellite.name) {
                    ex = s;
                    replaced = true;
                    break;
                }
            }
            if (!replaced) realSatCatalog.push_back(s);
            std::ostringstream st;
            st << "Fetched NORAD " << realFetchNoradRequested << (s.from_cache ? " (cache)" : " (network)");
            realSatStatus = st.str();
        } else {
            realSatStatus = "Fetch NORAD failed. See Diagnostics for details.";
        }
    };

    std::cout << "======================================================\n";
    std::cout << "  Satellite Downlink Simulator\n";
    std::cout << "  Controls available in the ImGui panel (left)\n";
    std::cout << "  Drag mouse to rotate | Scroll to zoom\n";
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
        glm::vec3 satPos = orbit.posScaled(orbit.time, EARTH_R);
        if (real_utc_mode && !paused && std::abs(time_warp - 1.0f) < 0.001f) {
            sim_unix = (double)std::time(nullptr);
        } else {
            sim_unix += sim_dt;
        }
        earth_rotation = (float)(gmstRadiansFromUnix(sim_unix));

        if (selectedSatIdx < 0 || selectedSatIdx >= (int)satellites.size()) selectedSatIdx = 0;
        if (selectedAntennaIdx < 0 || selectedAntennaIdx >= (int)antennas.size()) selectedAntennaIdx = 0;
        if (selectedSatIdx != lastSatIdx) {
            satScenario = satellites[selectedSatIdx];
            orbit.el = satScenario.elements;
            orbit.setMeanMotionOverride((satScenario.propagator == "sgp4_tle" && satScenario.tle_loaded) ? satScenario.tle_mean_motion_rad_s : 0.0);
            orbit.time = orbitStartFromUnix(satScenario, sim_unix);
            ui_alt_km   = (float)((satScenario.elements.a - phys::R_EARTH) / 1000.0);
            ui_ecc      = (float)satScenario.elements.e;
            ui_inc_deg  = (float)glm::degrees(satScenario.elements.i);
            ui_raan_deg = (float)glm::degrees(satScenario.elements.raan);
            ui_omg_deg  = (float)glm::degrees(satScenario.elements.omega);
            contacts.assign(antennas.size(), ContactSummary{});
            eventLog.clear();
            marginHist.fill(0.0f);
            throughputHist.fill(0.0f);
            for (size_t i = 0; i < marginHistByAntenna.size(); ++i) {
                marginHistByAntenna[i].fill(0.0f);
                throughputHistByAntenna[i].fill(0.0f);
            }
            histHead = 0;
            histCount = 0;
            selectedAntennaIdx = 0;
            trailCount = 0;
            deleteLineMesh(orbitPath);
            orbitPath = createOrbitPath(orbit, EARTH_R);
            lastSatIdx = selectedSatIdx;
        }

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

        int activeLinks = 0;
        int lineCount = 0;
        float bestMargin = -30.0f;
        float totalThroughput = 0.0f;
        const double worldToMeters = phys::R_EARTH / EARTH_R;
        for (size_t i = 0; i < antennas.size(); ++i) {
            AntennaScenario& ant = antennas[i];
            LinkTelemetry& lt = linkState[i];

            glm::vec3 local = localFromLatLon(ant.latitude_deg, ant.longitude_deg, EARTH_R);
            glm::vec3 stationPos = rotateY(local, earth_rotation);
            glm::vec3 upLocal = glm::normalize(local);
            glm::vec3 eastLocal = glm::normalize(glm::vec3(-std::sin(glm::radians(ant.longitude_deg)), 0.0f, std::cos(glm::radians(ant.longitude_deg))));
            glm::vec3 northLocal = glm::normalize(glm::cross(upLocal, eastLocal));

            glm::vec3 up = glm::normalize(rotateY(upLocal, earth_rotation));
            glm::vec3 east = glm::normalize(rotateY(eastLocal, earth_rotation));
            glm::vec3 north = glm::normalize(rotateY(northLocal, earth_rotation));

            glm::vec3 rangeVec = satPos - stationPos;
            float rangeWorld = glm::length(rangeVec);
            glm::vec3 ur = (rangeWorld > 0.0f) ? rangeVec / rangeWorld : glm::vec3(0, 1, 0);
            float elevationDeg = glm::degrees(std::asin(glm::clamp(glm::dot(ur, up), -1.0f, 1.0f)));
            float azDeg = glm::degrees(std::atan2(glm::dot(ur, east), glm::dot(ur, north)));
            if (azDeg < 0.0f) azDeg += 360.0f;

            float maxAzStep = ant.slew_az_deg_s * dt;
            float maxElStep = ant.slew_el_deg_s * dt;
            float daz = wrap180(azDeg - ant.current_az_deg);
            float del = elevationDeg - ant.current_el_deg;
            ant.current_az_deg = wrap360(ant.current_az_deg + glm::clamp(daz, -maxAzStep, maxAzStep));
            ant.current_el_deg += glm::clamp(del, -maxElStep, maxElStep);
            ant.current_el_deg = glm::clamp(ant.current_el_deg, -5.0f, 90.0f);

            bool visible = elevationDeg >= std::max(elevation_mask_deg, ant.min_elevation_deg);
            bool locked = visible
                       && (std::abs(wrap180(azDeg - ant.current_az_deg)) < 0.7f)
                       && (std::abs(elevationDeg - ant.current_el_deg) < 0.7f);

            float rangeKm = (float)((rangeWorld * worldToMeters) / 1000.0);
            float freqGHz = (float)(satScenario.downlink_freq_hz / 1e9);
            float fspl = 92.45f + 20.0f * std::log10(std::max(1.0f, rangeKm)) + 20.0f * std::log10(std::max(0.1f, freqGHz));
            float rain = visible ? rainFadeDb(rain_rate_mm_h, elevationDeg) : 0.0f;
            float atmLoss = visible ? (2.0f / std::max(0.25f, std::sin(glm::radians(std::max(1.0f, elevationDeg))))) : 0.0f;

            float rxPowerDbw = (float)(satScenario.tx_power_dbw + satScenario.tx_gain_dbi)
                             + ant.rx_gain_dbi - fspl - rain - atmLoss - ant.misc_losses_db;
            float tSys = std::max(30.0f, ant.system_temp_k);
            float n0DbwHz = -228.6f + 10.0f * std::log10(tSys);
            float cn0 = rxPowerDbw - n0DbwHz;
            float snrDb = cn0 - 10.0f * std::log10((float)std::max(1.0, satScenario.bandwidth_hz));
            float assumedBitrate = (float)std::max(1.0, satScenario.bandwidth_hz * 0.8);
            float ebn0 = cn0 - 10.0f * std::log10(assumedBitrate);
            float margin = ebn0 - (float)satScenario.required_ebn0_db;
            float ebn0Lin = std::pow(10.0f, ebn0 / 10.0f);
            float ber = 0.5f * std::erfc(std::sqrt(std::max(0.0f, ebn0Lin)));

            float throughputMbps = 0.0f;
            if (visible && locked) {
                if (use_shannon_model) {
                    float snrLin = std::pow(10.0f, snrDb / 10.0f);
                    double th = satScenario.bandwidth_hz * std::log2(1.0 + std::max(0.0, (double)snrLin)) * shannon_eff;
                    throughputMbps = (float)(th / 1e6);
                } else {
                    throughputMbps = throughputTableMbps(ebn0);
                }
            }

            lt.visible = visible;
            lt.locked = locked;
            lt.range_km = rangeKm;
            lt.elevation_deg = elevationDeg;
            lt.azimuth_deg = azDeg;
            lt.rain_loss_db = rain;
            lt.fspl_db = fspl;
            lt.cn0_dbhz = cn0;
            lt.ebn0_db = ebn0;
            lt.margin_db = margin;
            lt.ber = visible ? ber : 1.0f;
            lt.throughput_mbps = throughputMbps;

            bool linkActive = visible && locked;
            if (linkActive) {
                activeLinks++;
                bestMargin = std::max(bestMargin, margin);
                totalThroughput += throughputMbps;
                if (lineCount * 6 + 5 < (int)linkVerts.size()) {
                    linkVerts[lineCount * 6 + 0] = stationPos.x;
                    linkVerts[lineCount * 6 + 1] = stationPos.y;
                    linkVerts[lineCount * 6 + 2] = stationPos.z;
                    linkVerts[lineCount * 6 + 3] = satPos.x;
                    linkVerts[lineCount * 6 + 4] = satPos.y;
                    linkVerts[lineCount * 6 + 5] = satPos.z;
                    lineCount++;
                }
            }

            if (visible != ant.was_visible) {
                std::ostringstream e;
                e << "[" << utcStringFromUnix(sim_unix) << "] " << ant.name << " " << (visible ? "AOS" : "LOS");
                eventLog.push_back(e.str());
            }
            if (locked != ant.was_locked) {
                std::ostringstream e;
                e << "[" << utcStringFromUnix(sim_unix) << "] " << ant.name << " " << (locked ? "LOCK" : "UNLOCK");
                eventLog.push_back(e.str());
            }

            ContactSummary& cs = contacts[i];
            if (linkActive && !cs.in_contact) {
                cs.in_contact = true;
                cs.contact_start_sim = orbit.time;
                cs.pass_count++;
            } else if (!linkActive && cs.in_contact) {
                cs.in_contact = false;
                cs.last_contact_s = (float)(orbit.time - cs.contact_start_sim);
                cs.total_contact_s += cs.last_contact_s;
            }
            ant.was_visible = visible;
            ant.was_locked = locked;
        }

        histTimer += sim_dt;
        if (histTimer >= 1.0f) {
            histTimer = 0.0f;
            marginHist[histHead] = bestMargin;
            throughputHist[histHead] = totalThroughput;
            for (size_t i = 0; i < antennas.size(); ++i) {
                marginHistByAntenna[i][histHead] = linkState[i].margin_db;
                throughputHistByAntenna[i][histHead] = linkState[i].throughput_mbps;
            }
            histHead = (histHead + 1) % (int)marginHist.size();
            histCount = std::min((int)marginHist.size(), histCount + 1);
        }
        if (eventLog.size() > 120) {
            eventLog.erase(eventLog.begin(), eventLog.begin() + (eventLog.size() - 120));
        }
        glBindBuffer(GL_ARRAY_BUFFER, linkVBO);
        if (lineCount > 0) {
            glBufferSubData(GL_ARRAY_BUFFER, 0, lineCount * 6 * sizeof(float), linkVerts.data());
        }

        // ═══════════════════════════════════════════════════════════
        //  PANEL IMGUI
        // ═══════════════════════════════════════════════════════════

        const float uiGap = 10.0f;
        const float leftW = 560.0f;
        const float topH = 84.0f;
        const float leftX = uiGap;
        const float rightX = leftX + leftW + uiGap;
        const float rightW = std::max(320.0f, (float)WIN_W - rightX - uiGap);
        const float lowerY = uiGap + topH + uiGap;

        std::ostringstream kSat, kLinks, kThr, kMargin;
        kSat << satScenario.name;
        kLinks << activeLinks << " / " << antennas.size();
        kThr << std::fixed << std::setprecision(2) << totalThroughput << " Mbps";
        kMargin << std::fixed << std::setprecision(1) << bestMargin << " dB";

        if (realFetchInProgress && realFetchFuture.valid() &&
            realFetchFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            try {
                RealSatelliteFetch fetched = realFetchFuture.get();
                finalizeRealFetch(fetched);
            } catch (const std::exception& ex) {
                pushErr(cfgDiag, std::string("real_sats: async fetch failed: ") + ex.what());
                realSatStatus = "Fetch failed unexpectedly. See Diagnostics.";
            } catch (...) {
                pushErr(cfgDiag, "real_sats: async fetch failed with unknown exception");
                realSatStatus = "Fetch failed unexpectedly. See Diagnostics.";
            }
            realFetchInProgress = false;
        }

        ImGui::SetNextWindowPos(ImVec2(leftX, uiGap), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2((float)WIN_W - 2.0f * uiGap, topH), ImGuiCond_Always);
        ImGui::Begin("Mission Overview", nullptr,
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
        if (ImGui::BeginTable("overview_cards", 4, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextColumn(); drawMetricCard("card_sat", "SATELLITE", kSat.str(), ImVec4(0.75f, 0.88f, 0.98f, 1.0f));
            ImGui::TableNextColumn(); drawMetricCard("card_links", "ACTIVE LINKS", kLinks.str(), ImVec4(0.38f, 0.88f, 0.62f, 1.0f));
            ImGui::TableNextColumn(); drawMetricCard("card_thr", "TOTAL THROUGHPUT", kThr.str(), ImVec4(0.44f, 0.82f, 0.95f, 1.0f));
            ImGui::TableNextColumn(); drawMetricCard("card_margin", "BEST MARGIN", kMargin.str(), ImVec4(0.95f, 0.80f, 0.42f, 1.0f));
            ImGui::EndTable();
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(leftX, lowerY), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(leftW, (float)WIN_H - lowerY - uiGap), ImGuiCond_Always);
        ImGui::Begin("Mission Control");

        auto drawLinkStatus = [](const LinkTelemetry& lt) {
            if (!lt.visible) {
                ImGui::TextColored(ImVec4(0.90f, 0.27f, 0.27f, 1.0f), "RED");
            } else if (lt.locked && lt.margin_db >= 3.0f) {
                ImGui::TextColored(ImVec4(0.25f, 0.88f, 0.48f, 1.0f), "GREEN");
            } else {
                ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.28f, 1.0f), "YELLOW");
            }
        };

        if (ImGui::BeginTabBar("mission_tabs")) {
            if (ImGui::BeginTabItem("Ops")) {
                ImGui::SeparatorText("Time");
                ImGui::SliderFloat("Warp", &time_warp, 1.0f, 10000.0f, "x%.0f", ImGuiSliderFlags_Logarithmic);
                if (ImGui::Button(paused ? "Resume" : "Pause")) paused = !paused;
                ImGui::SameLine();
                if (ImGui::Button("Reset")) {
                    sim_unix = (double)std::time(nullptr);
                    orbit.time = orbitStartFromUnix(satScenario, sim_unix);
                    trailCount = 0;
                    earth_rotation = (float)(gmstRadiansFromUnix(sim_unix));
                }

                double elapsed = orbit.time;
                int hrs = (int)(elapsed / 3600.0);
                int mins = (int)(std::fmod(elapsed, 3600.0) / 60.0);
                int secs = (int)(std::fmod(elapsed, 60.0));
                ImGui::Text("Sim Time: %02d:%02d:%02d", hrs, mins, secs);
                ImGui::Text("UTC: %s", utcStringFromUnix(sim_unix).c_str());

                ImGui::SeparatorText("Satellite Selection");
                if (ImGui::BeginCombo("Active Satellite", satScenario.name.c_str())) {
                    for (int i = 0; i < (int)satellites.size(); ++i) {
                        bool selected = (i == selectedSatIdx);
                        if (ImGui::Selectable(satellites[i].name.c_str(), selected)) selectedSatIdx = i;
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                ImGui::SeparatorText("Orbital Info");
                ImGui::Text("Altitude: %.1f km", alt_km);
                ImGui::Text("Velocity: %.3f km/s", vel_kms);
                ImGui::Text("Period: %.1f min", period_min);
                ImGui::Text("Satellite: %s", satScenario.name.c_str());
                ImGui::Text("Satellites: %d | Antennas: %d | Active links: %d", (int)satellites.size(), (int)antennas.size(), activeLinks);

                ImGui::SeparatorText("Orbital Params");
                ImGui::SliderFloat("Altitude (km)", &ui_alt_km, 200.0f, 42000.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
                ImGui::SliderFloat("Eccentricity", &ui_ecc, 0.0f, 0.95f, "%.4f");
                ImGui::SliderFloat("Inclination", &ui_inc_deg, 0.0f, 180.0f, "%.1f deg");
                ImGui::SliderFloat("RAAN", &ui_raan_deg, -180.0f, 360.0f, "%.1f deg");
                ImGui::SliderFloat("Arg Periapsis", &ui_omg_deg, 0.0f, 360.0f, "%.1f deg");

                if (ImGui::Button("Apply Changes")) {
                    orbit.el.a     = phys::R_EARTH + (double)ui_alt_km * 1000.0;
                    orbit.el.e     = ui_ecc;
                    orbit.el.i     = glm::radians((double)ui_inc_deg);
                    orbit.el.raan  = glm::radians((double)ui_raan_deg);
                    orbit.el.omega = glm::radians((double)ui_omg_deg);
                    satScenario.propagator = "kepler";
                    orbit.setMeanMotionOverride(0.0);
                    satScenario.elements = orbit.el;
                    satellites[selectedSatIdx] = satScenario;
                    orbit.time = orbitStartFromUnix(satScenario, sim_unix);
                    trailCount = 0;
                    deleteLineMesh(orbitPath);
                    orbitPath = createOrbitPath(orbit, EARTH_R);
                }

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
                        satScenario.propagator = "kepler";
                        orbit.setMeanMotionOverride(0.0);
                        satScenario.elements = orbit.el;
                        satellites[selectedSatIdx] = satScenario;
                        orbit.time = orbitStartFromUnix(satScenario, sim_unix);
                        trailCount = 0;
                        deleteLineMesh(orbitPath);
                        orbitPath = createOrbitPath(orbit, EARTH_R);
                    }
                }

                if (ImGui::Button("Reload JSON")) {
                    cfgDiag = {};
                    satellites = loadSatelliteScenarios("config/satellites.json", cfgDiag);
                    selectedSatIdx = 0;
                    selectedAntennaIdx = 0;
                    satScenario = satellites[selectedSatIdx];
                    antennas = loadAntennaScenario("config/antennas.json", cfgDiag);
                    simScenario = loadSimScenario("config/sim.json", cfgDiag);
                    use_shannon_model = simScenario.use_shannon_model;
                    rain_rate_mm_h = simScenario.rain_rate_mm_h;
                    shannon_eff = simScenario.shannon_efficiency;
                    elevation_mask_deg = simScenario.elevation_mask_deg;

                    linkState.assign(antennas.size(), LinkTelemetry{});
                    contacts.assign(antennas.size(), ContactSummary{});
                    marginHistByAntenna.assign(antennas.size(), std::array<float, 240>{});
                    throughputHistByAntenna.assign(antennas.size(), std::array<float, 240>{});
                    linkVerts.assign(std::max(1, (int)antennas.size()) * 6, 0.0f);
                    glBindBuffer(GL_ARRAY_BUFFER, linkVBO);
                    glBufferData(GL_ARRAY_BUFFER, linkVerts.size() * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

                    orbit.el = satScenario.elements;
                    orbit.setMeanMotionOverride((satScenario.propagator == "sgp4_tle" && satScenario.tle_loaded) ? satScenario.tle_mean_motion_rad_s : 0.0);
                    ui_alt_km   = (float)((satScenario.elements.a - phys::R_EARTH) / 1000.0);
                    ui_ecc      = (float)satScenario.elements.e;
                    ui_inc_deg  = (float)glm::degrees(satScenario.elements.i);
                    ui_raan_deg = (float)glm::degrees(satScenario.elements.raan);
                    ui_omg_deg  = (float)glm::degrees(satScenario.elements.omega);
                    sim_unix = (double)std::time(nullptr);
                    orbit.time = orbitStartFromUnix(satScenario, sim_unix);
                    earth_rotation = (float)(gmstRadiansFromUnix(sim_unix));
                    lastSatIdx = selectedSatIdx;
                    trailCount = 0;
                    deleteLineMesh(orbitPath);
                    orbitPath = createOrbitPath(orbit, EARTH_R);
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Links")) {
                ImGui::SeparatorText("Communication Model");
                ImGui::Checkbox("Real UTC default", &real_utc_mode);
                ImGui::SliderFloat("Rain rate (mm/h)", &rain_rate_mm_h, 0.0f, 40.0f, "%.1f");
                ImGui::SliderFloat("Elevation mask", &elevation_mask_deg, 0.0f, 30.0f, "%.1f deg");
                ImGui::Checkbox("Shannon throughput", &use_shannon_model);
                if (use_shannon_model) ImGui::SliderFloat("Shannon efficiency", &shannon_eff, 0.1f, 1.0f, "%.2f");

                if (ImGui::BeginTable("links", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY, ImVec2(0, 260))) {
                    ImGui::TableSetupColumn("Status");
                    ImGui::TableSetupColumn("Antenna");
                    ImGui::TableSetupColumn("Link");
                    ImGui::TableSetupColumn("El (deg)");
                    ImGui::TableSetupColumn("Range (km)");
                    ImGui::TableSetupColumn("Margin (dB)");
                    ImGui::TableSetupColumn("Thr (Mbps)");
                    ImGui::TableHeadersRow();
                    for (size_t i = 0; i < antennas.size(); ++i) {
                        const LinkTelemetry& lt = linkState[i];
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0); drawLinkStatus(lt);
                        ImGui::TableSetColumnIndex(1);
                        if (ImGui::Selectable((antennas[i].name + "##sel").c_str(), selectedAntennaIdx == (int)i, ImGuiSelectableFlags_SpanAllColumns)) {
                            selectedAntennaIdx = (int)i;
                        }
                        ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(lt.visible ? (lt.locked ? "LOCK" : "VISIBLE") : "NO LINK");
                        ImGui::TableSetColumnIndex(3); ImGui::Text("%.1f", lt.elevation_deg);
                        ImGui::TableSetColumnIndex(4); ImGui::Text("%.0f", lt.range_km);
                        ImGui::TableSetColumnIndex(5); ImGui::Text("%.1f", lt.margin_db);
                        ImGui::TableSetColumnIndex(6); ImGui::Text("%.2f", lt.throughput_mbps);
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Contacts")) {
                if (ImGui::BeginTable("contacts", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders, ImVec2(0, 220))) {
                    ImGui::TableSetupColumn("Antenna");
                    ImGui::TableSetupColumn("Passes");
                    ImGui::TableSetupColumn("Current (s)");
                    ImGui::TableSetupColumn("Last (s)");
                    ImGui::TableHeadersRow();
                    for (size_t i = 0; i < antennas.size(); ++i) {
                        float currentS = contacts[i].in_contact ? (float)(orbit.time - contacts[i].contact_start_sim) : 0.0f;
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(antennas[i].name.c_str());
                        ImGui::TableSetColumnIndex(1); ImGui::Text("%d", contacts[i].pass_count);
                        ImGui::TableSetColumnIndex(2); ImGui::Text("%.1f", currentS);
                        ImGui::TableSetColumnIndex(3); ImGui::Text("%.1f", contacts[i].last_contact_s);
                    }
                    ImGui::EndTable();
                }

                if (ImGui::Button("Export contacts CSV")) {
                    std::ofstream out("contact_report.csv");
                    if (!out.good()) {
                        exportStatus = "Could not write contact_report.csv";
                    } else {
                        out << "antenna,satellite,passes,total_contact_s,last_contact_s,in_contact,current_contact_s\n";
                        for (size_t i = 0; i < antennas.size(); ++i) {
                            float currentS = contacts[i].in_contact ? (float)(orbit.time - contacts[i].contact_start_sim) : 0.0f;
                            out << antennas[i].name << ","
                                << satScenario.name << ","
                                << contacts[i].pass_count << ","
                                << contacts[i].total_contact_s << ","
                                << contacts[i].last_contact_s << ","
                                << (contacts[i].in_contact ? 1 : 0) << ","
                                << currentS << "\n";
                        }
                        exportStatus = "Exported: contact_report.csv";
                    }
                }
                if (!exportStatus.empty()) ImGui::TextUnformatted(exportStatus.c_str());
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("History")) {
                static std::array<float, 240> marginView{};
                static std::array<float, 240> throughputView{};
                if (histCount > 0) {
                    for (int i = 0; i < histCount; ++i) {
                        int idx = (histHead - histCount + i + (int)marginHist.size()) % (int)marginHist.size();
                        marginView[i] = marginHist[idx];
                        throughputView[i] = throughputHist[idx];
                    }
                    ImGui::PlotLines("Margin History (dB)", marginView.data(), histCount, 0, nullptr, -20.0f, 30.0f, ImVec2(0, 70));
                    ImGui::PlotLines("Throughput History (Mbps)", throughputView.data(), histCount, 0, nullptr, 0.0f, 20.0f, ImVec2(0, 70));

                    for (size_t a = 0; a < antennas.size(); ++a) {
                        if (ImGui::TreeNode(("Antenna Plot: " + antennas[a].name).c_str())) {
                            static std::array<float, 240> antMarginView{};
                            static std::array<float, 240> antThrView{};
                            for (int i = 0; i < histCount; ++i) {
                                int idx = (histHead - histCount + i + (int)marginHist.size()) % (int)marginHist.size();
                                antMarginView[i] = marginHistByAntenna[a][idx];
                                antThrView[i] = throughputHistByAntenna[a][idx];
                            }
                            ImGui::PlotLines("Margin", antMarginView.data(), histCount, 0, nullptr, -20.0f, 30.0f, ImVec2(0, 50));
                            ImGui::PlotLines("Throughput", antThrView.data(), histCount, 0, nullptr, 0.0f, 20.0f, ImVec2(0, 50));
                            ImGui::TreePop();
                        }
                    }
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Visual")) {
                ImGui::Checkbox("Orbit", &show_orbit);
                ImGui::SameLine(); ImGui::Checkbox("Axes", &show_axes);
                ImGui::Checkbox("Stars", &show_stars);
                ImGui::SameLine(); ImGui::Checkbox("Trail", &show_trail);
                ImGui::Checkbox("Earth texture", &use_texture);
                ImGui::Checkbox("Antennas", &show_antennas);
                ImGui::SameLine(); ImGui::Checkbox("Links", &show_links);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Diagnostics")) {
                ImGui::Text("Warnings: %d | Errors: %d", (int)cfgDiag.warnings.size(), (int)cfgDiag.errors.size());
                ImGui::Separator();
                if (ImGui::BeginChild("diag_tab_scroll", ImVec2(0, 0), true)) {
                    for (const std::string& w : cfgDiag.warnings) {
                        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.25f, 1.0f), "WARN: %s", w.c_str());
                    }
                    for (const std::string& e : cfgDiag.errors) {
                        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "ERR : %s", e.c_str());
                    }
                    ImGui::EndChild();
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Events")) {
                if (ImGui::BeginChild("events_tab_scroll", ImVec2(0, 0), true)) {
                    for (const std::string& e : eventLog) {
                        ImGui::TextUnformatted(e.c_str());
                    }
                    if (!eventLog.empty()) {
                        ImGui::SetScrollHereY(1.0f);
                    }
                    ImGui::EndChild();
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Real Satellites")) {
                ImGui::SeparatorText("CelesTrak Import");
                ImGui::SliderInt("Top count", &realTopCount, 5, 50);
                ImGui::SliderInt("Cache TTL (hours)", &realCacheTtlHours, 1, 168);
                ImGui::InputInt("NORAD ID", &realNoradId);
                realNoradId = std::max(1, realNoradId);

                if (realFetchInProgress) {
                    const char spinner[4] = {'|', '/', '-', '\\'};
                    int frame = ((int)(glfwGetTime() * 8.0)) & 3;
                    ImGui::Text("Loading %c Fetch in progress...", spinner[frame]);
                }

                ImGui::BeginDisabled(realFetchInProgress);

                if (ImGui::Button("Fetch Top")) {
                    const int topCount = realTopCount;
                    const int ttlSeconds = realCacheTtlHours * 3600;
                    realFetchWasTop = true;
                    realFetchNoradRequested = 0;
                    realSatStatus = "Fetching top satellites...";
                    realFetchFuture = std::async(std::launch::async, [topCount, ttlSeconds]() {
                        return fetchTopRealSatellites(topCount, ttlSeconds);
                    });
                    realFetchInProgress = true;
                }

                ImGui::SameLine();
                if (ImGui::Button("Fetch NORAD")) {
                    const int norad = realNoradId;
                    const int ttlSeconds = realCacheTtlHours * 3600;
                    realFetchWasTop = false;
                    realFetchNoradRequested = norad;
                    realSatStatus = "Fetching NORAD " + std::to_string(norad) + "...";
                    realFetchFuture = std::async(std::launch::async, [norad, ttlSeconds]() {
                        return fetchRealSatelliteByNorad(norad, ttlSeconds);
                    });
                    realFetchInProgress = true;
                }

                ImGui::EndDisabled();

                ImGui::TextWrapped("%s", realSatStatus.c_str());

                if (ImGui::BeginTable("real_sats", 8, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY, ImVec2(0, 270))) {
                    ImGui::TableSetupColumn("Name");
                    ImGui::TableSetupColumn("NORAD");
                    ImGui::TableSetupColumn("Alt (km)");
                    ImGui::TableSetupColumn("TLE age (h)");
                    ImGui::TableSetupColumn("Cache");
                    ImGui::TableSetupColumn("Active");
                    ImGui::TableSetupColumn("Import");
                    ImGui::TableSetupColumn("Track");
                    ImGui::TableHeadersRow();

                    for (int i = 0; i < (int)realSatCatalog.size(); ++i) {
                        const RealSatelliteEntry& rs = realSatCatalog[i];
                        float altKm = (float)((rs.satellite.elements.a - phys::R_EARTH) / 1000.0);
                        float ageHours = (float)std::max(0.0, ((double)std::time(nullptr) - rs.satellite.tle_epoch_unix) / 3600.0);

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        if (ImGui::Selectable((rs.satellite.name + "##realsat").c_str(), realSelectedRow == i)) {
                            realSelectedRow = i;
                        }
                        ImGui::TableSetColumnIndex(1);
                        if (rs.norad_id > 0) ImGui::Text("%d", rs.norad_id); else ImGui::TextUnformatted("-");
                        ImGui::TableSetColumnIndex(2); ImGui::Text("%.1f", altKm);
                        ImGui::TableSetColumnIndex(3); ImGui::Text("%.1f", ageHours);
                        ImGui::TableSetColumnIndex(4); ImGui::TextUnformatted(rs.from_cache ? "yes" : "no");
                        ImGui::TableSetColumnIndex(5);
                        bool isActive = (selectedSatIdx >= 0 && selectedSatIdx < (int)satellites.size())
                                     && (satellites[selectedSatIdx].name == rs.satellite.name);
                        ImGui::TextUnformatted(isActive ? "yes" : "no");
                        ImGui::TableSetColumnIndex(6);
                        if (ImGui::SmallButton(("Import##" + std::to_string(i)).c_str())) {
                            int importedIdx = upsertImportedSatellite(rs);
                            std::ostringstream st;
                            st << "Imported " << rs.satellite.name << " at slot " << importedIdx << ". Use Track to activate.";
                            realSatStatus = st.str();
                        }
                        ImGui::TableSetColumnIndex(7);
                        if (ImGui::SmallButton(("Track##" + std::to_string(i)).c_str())) {
                            selectedSatIdx = upsertImportedSatellite(rs);
                            lastSatIdx = -1; // Force satellite re-activation logic in next frame.
                            std::ostringstream ev;
                            ev << "[" << utcStringFromUnix(sim_unix) << "] TRACK " << rs.satellite.name;
                            eventLog.push_back(ev.str());
                            realSatStatus = "Tracking " + rs.satellite.name;
                        }
                    }
                    ImGui::EndTable();
                }

                ImGui::Text("Imported satellites in scenario: %d", (int)satellites.size());
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();

        if (!antennas.empty() && selectedAntennaIdx >= 0 && selectedAntennaIdx < (int)antennas.size()) {
            const LinkTelemetry& flt = linkState[selectedAntennaIdx];
            ImGui::SetNextWindowPos(ImVec2((float)WIN_W - 300.0f, uiGap + topH + 6.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowBgAlpha(0.90f);
            ImGui::Begin("Link Focus", nullptr,
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoCollapse);
            ImGui::Text("Antenna: %s", antennas[selectedAntennaIdx].name.c_str());
            ImGui::Text("Satellite: %s", satScenario.name.c_str());

            if (!flt.visible) {
                ImGui::TextColored(ImVec4(0.90f, 0.27f, 0.27f, 1.0f), "Status: RED");
            } else if (flt.locked && flt.margin_db >= 3.0f) {
                ImGui::TextColored(ImVec4(0.25f, 0.88f, 0.48f, 1.0f), "Status: GREEN");
            } else {
                ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.28f, 1.0f), "Status: YELLOW");
            }

            ImGui::Separator();
            ImGui::Text("Elevation: %.1f deg", flt.elevation_deg);
            ImGui::Text("Range: %.0f km", flt.range_km);
            ImGui::Text("C/N0: %.1f dB-Hz", flt.cn0_dbhz);
            ImGui::Text("Eb/N0: %.1f dB", flt.ebn0_db);
            ImGui::Text("Margin: %.1f dB", flt.margin_db);
            ImGui::Text("Throughput: %.2f Mbps", flt.throughput_mbps);
            ImGui::Text("BER: %.3e", flt.ber);
            ImGui::End();
        }

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

        glm::vec3 lightDir = sunDirectionFromUnix(sim_unix);
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

        // --- Antenas ---
        if (show_antennas) {
            for (size_t i = 0; i < antennas.size(); ++i) {
                glm::vec3 local = localFromLatLon(antennas[i].latitude_deg, antennas[i].longitude_deg, EARTH_R * 1.01f);
                glm::vec3 stationPos = rotateY(local, earth_rotation);
                glm::mat4 stModel = glm::translate(glm::mat4(1.0f), stationPos) * glm::scale(glm::mat4(1.0f), glm::vec3(0.18f));
                glUniformMatrix4fv(glGetUniformLocation(progSphere,"uModel"),1,GL_FALSE,glm::value_ptr(stModel));
                if (linkState[i].visible) glUniform3f(glGetUniformLocation(progSphere,"uColor"),0.2f,0.9f,0.3f);
                else glUniform3f(glGetUniformLocation(progSphere,"uColor"),0.85f,0.25f,0.25f);
                glUniform1f(glGetUniformLocation(progSphere,"uAmbient"),0.55f);
                glUniform1i(glGetUniformLocation(progSphere,"uUseTexture"), 0);
                glBindVertexArray(satMesh.vao);
                glDrawArrays(GL_TRIANGLES, 0, satMesh.vertexCount);
            }
        }

        // --- Satellite ---
        float satToCamDist = glm::length(camPos - satPos);
        float satVisualScale = glm::clamp(0.018f * satToCamDist, 1.2f, 14.0f);
        glm::mat4 satModel = glm::translate(glm::mat4(1.0f), satPos)
                   * glm::scale(glm::mat4(1.0f), glm::vec3(satVisualScale));
        glUniformMatrix4fv(glGetUniformLocation(progSphere,"uModel"),1,GL_FALSE,glm::value_ptr(satModel));
        glUniform3f(glGetUniformLocation(progSphere,"uColor"),0.9f,0.85f,0.2f);
        glUniform1f(glGetUniformLocation(progSphere,"uAmbient"),0.4f);
        glUniform1i(glGetUniformLocation(progSphere,"uUseTexture"), 0);
        glBindVertexArray(satMesh.vao);
        glDrawArrays(GL_TRIANGLES, 0, satMesh.vertexCount);

        // --- Enlaces activos ---
        if (show_links && lineCount > 0) {
            glUseProgram(progLine);
            glUniformMatrix4fv(glGetUniformLocation(progLine, "uMVP"), 1, GL_FALSE, glm::value_ptr(mvp));
            glUniform3f(glGetUniformLocation(progLine, "uColor"), 0.1f, 0.95f, 0.95f);
            glBindVertexArray(linkVAO);
            glLineWidth(1.8f);
            glDrawArrays(GL_LINES, 0, lineCount * 2);
        }

        // --- Titulo ventana ---
        {
            std::ostringstream t;
            t << std::fixed << std::setprecision(1)
              << "Orbital Simulator | Alt: " << alt_km << " km"
              << " | Vel: " << vel_kms << " km/s"
              << " | Warp: x" << time_warp
                            << " | Links: " << activeLinks
              << (paused ? " [PAUSED]" : "");
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
    glDeleteVertexArrays(1,&linkVAO); glDeleteBuffers(1,&linkVBO);
    if (earthTex) glDeleteTextures(1, &earthTex);
    glDeleteProgram(progSphere); glDeleteProgram(progLine);

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
