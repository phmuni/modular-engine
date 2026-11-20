# Copies the folder FROM into TO (created if needed), leaving out macOS's .DS_Store files. Run with
#   cmake -DFROM=<folder> -DTO=<folder> -P copyAssetFolder.cmake
# by modular_engine_add_game to put the engine's own assets next to a game.
if(NOT FROM OR NOT TO)
  message(FATAL_ERROR "copyAssetFolder.cmake needs -DFROM=<folder> -DTO=<folder>")
endif()
file(COPY "${FROM}/" DESTINATION "${TO}" PATTERN ".DS_Store" EXCLUDE)
