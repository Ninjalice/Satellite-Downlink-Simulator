# 🛰️ Simulador Orbital — Tierra + Satélite

Simulador 3D que renderiza la Tierra con un satélite orbitando usando mecánica Kepleriana y OpenGL 3.3.

## Características

- **Mecánica orbital real**: Resolución de la ecuación de Kepler (Newton-Raphson)
- **Órbita ISS**: LEO a 408 km, inclinación 51.6°, e ≈ 0.0007
- **Renderizado 3D**: Tierra iluminada (Blinn-Phong), satélite, trayectoria orbital
- **Cámara interactiva**: Rotar, hacer zoom
- **Datos en tiempo real**: Altitud, velocidad orbital en la barra de título
- **Estrellas de fondo** y trail del satélite
- **Control temporal**: Acelerar, pausar, reiniciar

## Requisitos

- **Windows 10/11**
- **CMake** ≥ 3.16 — [Descargar](https://cmake.org/download/)
- **Git** — [Descargar](https://git-scm.com/)
- **Compilador C++17**: Visual Studio 2019/2022, MinGW, o Clang
- **GPU con OpenGL 3.3** (cualquier GPU moderna)

## Compilación

### Opción rápida

```batch
build.bat
```

### Manual

```batch
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
Release\simulator.exe
```

## Controles

| Tecla           | Acción                        |
| --------------- | ----------------------------- |
| Arrastrar ratón | Rotar cámara                  |
| Scroll          | Zoom                          |
| `+` / `-`       | Acelerar / desacelerar tiempo |
| `Espacio`       | Pausar / reanudar             |
| `R`             | Reiniciar simulación          |
| `ESC`           | Salir                         |

## Parámetros orbitales (ISS)

| Parámetro      | Valor     |
| -------------- | --------- |
| Semi-eje mayor | 6,779 km  |
| Excentricidad  | 0.0007    |
| Inclinación    | 51.6°     |
| Periodo        | ~92.7 min |
| Altitud        | ~408 km   |

## Estructura

```
SIMULATOR/
├── main.cpp          ← Todo el código del simulador
├── CMakeLists.txt    ← Configuración de compilación (descarga deps automáticamente)
├── build.bat         ← Script de compilación rápida
└── README.md
```

## Modificar la órbita

Edita los elementos orbitales en `main.cpp` (busca `OrbitalElements iss_elements`):

```cpp
// Ejemplo: Órbita GEO (geoestacionaria)
iss_elements.a     = phys::R_EARTH + 35786000.0;  // 35,786 km de altitud
iss_elements.e     = 0.0;                          // circular
iss_elements.i     = 0.0;                          // ecuatorial
```

```cpp
// Ejemplo: Órbita Molniya
iss_elements.a     = 26600000.0;                   // semi-eje mayor
iss_elements.e     = 0.74;                         // muy excéntrica
iss_elements.i     = glm::radians(63.4);           // inclinación crítica
```
