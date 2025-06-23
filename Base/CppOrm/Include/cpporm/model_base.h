// cpporm/model_base.h
#ifndef cpporm_MODEL_BASE_H
#define cpporm_MODEL_BASE_H

// This is now a convenience header that includes all the logical parts
// of the model definition system in the correct order.
// Files that previously included "cpporm/model_base.h" do not need to be
// changed.

#include "cpporm/model_base_class.h"
#include "cpporm/model_crtp_base.h"
#include "cpporm/model_meta.h"
#include "cpporm/model_meta_definitions.h"
#include "cpporm/model_registry.h"
#include "cpporm/model_types.h"

#endif  // cpporm_MODEL_BASE_H