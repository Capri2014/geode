set(module_SRCS
  base64.cpp
  CloneArray.cpp
  curry.cpp
  debug.cpp
  DebugPrint.cpp
  format.cpp
  interrupts.cpp
  Log.cpp
  LogEntry.cpp
  LogScope.cpp
  module.cpp
  path.cpp
  process.cpp
  ProgressIndicator.cpp
  resource.cpp
  stream.cpp
  time.cpp
)

set(module_HEADERS
  base64.h
  Cloneable.h
  CloneArray.h
  compose.h
  config.h
  const_cast.h
  convert_case.h
  CopyConst.h
  c_str.h
  curry.h
  debug.h
  DebugPrint.h
  endian.h
  enumerate.h
  equals.h
  format.h
  forward.h
  from_string.h
  function.h
  HasCheapCopy.h
  Hasher.h
  IdSet.h
  interrupts.h
  IRange.h
  LogEntry.h
  Log.h
  LogScope.h
  macro_map.h
  move.h
  mpl.h
  openmp.h
  overload.h
  pass.h
  path.h
  prioritize.h
  process.h
  ProgressIndicator.h
  Protect.h
  range.h
  remove_commas.h
  resource.h
  rounding.h
  SanitizeFunction.h
  smart_ptr.h
  STATIC_ASSERT_SAME.h
  stl.h
  stream.h
  str.h
  time.h
  tr1.h
  type_traits.h
  Unique.h
  using.h
  validity.h
)

install_geode_headers(utility ${module_HEADERS})

add_geode_module(utility ${module_SRCS})
