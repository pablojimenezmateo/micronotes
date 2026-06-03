#include "app/Application.h"

int main(int argc, char** argv) {
  return micronotes::app::run(micronotes::app::parseArgs(argc, argv));
}
