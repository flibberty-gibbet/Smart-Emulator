// Convenience header: the emulator's machine built on the project's own CPU core.
#pragma once
#include "cpu.h"
#include "machine.h"
namespace tsmart { using Machine = MachineT<Cpu, Bus>; }
