#pragma once

#include <stdint.h>
#include "pet/pet.h"

void pet_render(uint16_t *dst, int w, int h, const PetView &view);
