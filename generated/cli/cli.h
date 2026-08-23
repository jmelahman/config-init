#pragma once
#include "so/builtin/builtin.h"
#include "so/bytes/bytes.h"
#include "so/errors/errors.h"
#include "so/fmt/fmt.h"
#include "so/mem/mem.h"
#include "so/os/os.h"
#include "so/strings/strings.h"

// -- Variables and constants --

// Version is the config-init release version.
static const so_String cli_Version = so_str("0.1.0");

// -- Functions and methods --

// Run executes the CLI and returns the process exit code.
so_int cli_Run(so_Slice args);
