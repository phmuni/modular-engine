@echo off
setlocal enabledelayedexpansion

:: Limpar a variável SRC_FILES antes de usá-la
set SRC_FILES=

:: Encontrar todos os arquivos .cpp dentro da pasta src e suas subpastas
for /R src %%f in (*.cpp) do (
    set SRC_FILES=!SRC_FILES! "%%f"
)

:: Compilar todos os arquivos .cpp encontrados
clang++ -g -I include -I lib/include -I lib/include/imgui -I lib/include/imgui/backends -I lib/include/imgui/misc/cpp -I lib/include/assimp -L lib/lib !SRC_FILES! -lSDL3 -lassimp-vc143-mt -o bin/main.exe

:: Rodar o executável
cd bin
main
cd ..
