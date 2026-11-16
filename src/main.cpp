#include "Game.hpp"

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  Game game;
  if (!game.Initialize()) {
    return 1;
  }

  game.RunLoop();
  game.Shutdown();
  return 0;
}