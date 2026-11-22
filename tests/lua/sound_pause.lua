-- sound: the option that makes music pause together with the game (the pause itself is tested in audioTests.cpp)

test.run(function()
  test.eq(sound.musicPausesWithGame(), false, "music ignores the pause by default")
  sound.setMusicPausesWithGame(true)
  test.eq(sound.musicPausesWithGame(), true, "turned on")
  sound.setMusicPausesWithGame(false)
  test.eq(sound.musicPausesWithGame(), false, "turned off")
end)
