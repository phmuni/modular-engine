-- sound: the option that makes music pause together with the game (the pause itself is tested in audioTests.cpp)

test.run(function()
  test.eq(sound.musicPausesWithGame(), true, "music pauses with the pause menu by default")
  sound.setMusicPausesWithGame(false)
  test.eq(sound.musicPausesWithGame(), false, "turned off")
  sound.setMusicPausesWithGame(true)
  test.eq(sound.musicPausesWithGame(), true, "turned on")
end)
