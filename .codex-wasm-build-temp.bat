@echo off
call C:\Users\zachf\scoop\apps\emscripten\6.0.11\emsdk_env.bat
set "PATH=C:\Users\zachf\scoop\apps\emscripten\6.0.11\upstream\emscripten;%PATH%"
where emcc
cmake --build build/web-wasm --target client-web
