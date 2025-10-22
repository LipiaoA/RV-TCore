#include <iostream>

#include "model.h"

uint64_t g_cycles = 0;

int main(int argc, void *argv[]) {

    if (argc < 2) return 0;
    std::string elf_path = "";
    Model *model;
    model = new Model("top model");
    model->init(elf_path);
    model->build();
    return 0;
}
