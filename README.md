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
- **Escenarios por JSON**: Carga de `config/satellites.json`, `config/antennas.json`, `config/sim.json`
- **Antenas terrestres**: Posicionamiento geográfico sobre el globo y seguimiento automático
- **Simulación de enlace**: visibilidad, AOS/LOS, lock/unlock, FSPL, C/N0, Eb/N0, margen, BER y throughput
- **Modelo de throughput seleccionable**: Shannon o tabla por umbrales de Eb/N0
- **Ruta TLE**: Soporte de `propagator: "sgp4_tle"` con `tle.line1` y `tle.line2` en `satellites.json`
- **Múltiples satélites**: `satellites.json` ahora acepta arreglo `satellites` y selección de satélite activo en UI
- **Validación JSON reforzada**: Reporte de warnings/errores de rango, tipos y campos requeridos en panel de diagnóstico
- **Resumen de contactos**: Conteo de pases por antena + duración de contacto actual/último
- **Plots en tiempo real**: Historial de margen y throughput en UI
- **Export de reporte**: Botón para generar `contact_report.csv`

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
├── main.cpp          ← Entrada principal, UI y render loop
├── CMakeLists.txt    ← Configuración de compilación (descarga deps automáticamente)
├── build.bat         ← Script de compilación rápida
├── src/
│   ├── scenario.h    ← Tipos de dominio + API de carga de configuración
│   ├── scenario.cpp  ← Parseo/validación JSON de satélites, antenas y simulación
│   ├── orbit.h       ← API de utilidades orbitales/geográficas
│   └── orbit.cpp     ← Implementación de propagación Kepler y helpers matemáticos
├── config/
│   ├── satellites.json
│   ├── antennas.json
│   └── sim.json
└── README.md
```

## Configuración de escenario (JSON)

El simulador intenta cargar los siguientes archivos al iniciar:

- `config/satellites.json`
- `config/antennas.json`
- `config/sim.json`

Si alguno no existe o tiene formato inválido, se usan valores por defecto y se muestra advertencia por consola.

En UI puedes pulsar **Reload JSON** para recargar en caliente.

## Estado actual de implementación

- El flujo de configuración por JSON ya está activo.
- La visualización y seguimiento de antenas en el globo ya está activo.
- La simulación de comunicaciones base ya está activa en tiempo real.
- El modo TLE (aproximación basada en campos orbitales TLE) está activo.
- Selección de satélite activo y simulación por escenario multi-satélite está activo.

## Modificar la órbita

Puedes modificar la órbita desde la UI (pestaña Ops), o editando los escenarios JSON en `config/satellites.json`.

Si necesitas cambiar comportamiento orbital a nivel código, revisa `src/orbit.cpp` y el uso en `main.cpp`.

Ejemplo de parámetros en JSON:

```cpp
{
	"name": "GEO Demo",
	"propagator": "kepler",
	"orbit": {
		"altitude_km": 35786,
		"eccentricity": 0.0,
		"inclination_deg": 0.0,
		"raan_deg": 0.0,
		"arg_periapsis_deg": 0.0,
		"mean_anomaly_deg": 0.0
	}
}
```
