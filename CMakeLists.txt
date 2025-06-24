cmake_minimum_required(VERSION 3.10)

# Nome do Projeto
project(Engine)

# Definir o padrão de compilação
set(CMAKE_CXX_STANDARD 17)

# Definir os diretórios de saída
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")

# Definir o diretório onde as bibliotecas externas estão localizadas
set(EXTERNAL_DIR "${CMAKE_SOURCE_DIR}/external")

# Adicionar diretórios de include para as dependências
include_directories(
    ${CMAKE_SOURCE_DIR}/internal
    ${EXTERNAL_DIR}
)

# Encontrar todos os arquivos .cpp recursivamente dentro do diretório src
file(GLOB_RECURSE SOURCE_FILES "${CMAKE_SOURCE_DIR}/src/**/*.cpp" "${CMAKE_SOURCE_DIR}/external/**/*.cpp")

# Criar o executável com os arquivos encontrados
add_executable(engine ${SOURCE_FILES})

# Linkar as bibliotecas externas manualmente
target_link_libraries(engine
    ${EXTERNAL_DIR}/SDL3/lib/libSDL3.dll.a
    # Linkar outras bibliotecas necessárias
)

# Copiar todas as DLLs das dependências para a pasta bin
file(GLOB DLL_FILES "${EXTERNAL_DIR}/*/bin/*.dll")
foreach(DLL ${DLL_FILES})
    add_custom_command(TARGET engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLL}" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
    )
endforeach()