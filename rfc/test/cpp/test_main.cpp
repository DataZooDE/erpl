#define CATCH_CONFIG_RUNNER
#include <iostream>
#include "catch.hpp"

int main(int argc, char *argv[]) {
  std::cout << std::endl << "**** ERPL CPP Unit Tests ****" << std::endl << std::endl;

  int result = Catch::Session().run( argc, argv );
  return result;
}
