src = files([
  'edi_suite.h',
  'edi_suite.c',
  'edi_test_content_provider.c',
  'edi_test_create.c',
  'edi_test_exe.c',
  'edi_test_language_provider.c',
  'edi_test_language_provider_c.c',
  'edi_test_path.c',
])

check = dependency('check')

deps = [elm, check, edi_lib, intl]
incls = [lib_dir, bin_dir, top_inc]

if get_option('libclang') == true
   deps += [clang]
   incls += [clang_inc]
endif

exe = executable('edi_suite', src,
  dependencies : deps,
  include_directories : incls,
  install : false
)
test('Edi Test Suite', exe)

