tar = find_program('tar')
edje_cc = find_program('edje_cc')

skeleton_names = ['eflproject', 'eflproject_python']

foreach skeleton_name : skeleton_names

  custom_target('skeleton ' + skeleton_name,
     command : [tar, 'zcf', '@OUTPUT@', '-C', '../data/extra/templates/skeletons', skeleton_name],
     input : 'skeletons' + '/' + skeleton_name,
     output : skeleton_name + '.tar.gz',
     install_dir: join_paths(get_option('prefix'), get_option('datadir'), 'edi', 'templates', 'skeletons'),
     install : true,
  )

endforeach

cmd = [ edje_cc,
        '-id', join_paths(meson.source_root(), 'data' , 'extra', 'templates', 'images'),
        '@INPUT@', '@OUTPUT@'
      ]

skeleton_meta = ['eflproject', 'eflproject_python']

foreach meta : skeleton_meta
    custom_target('meta ' + meta,
        input   : meta + '.edc',
        output  : meta + '.edj',
        command : cmd,
        install_dir: join_paths(get_option('prefix'), get_option('datadir'), 'edi', 'templates'),
        install: true,
    )
    out = join_paths(get_option('prefix'), get_option('datadir'), 'edi', 'templates') + '/' + meta + '.edj'
    meson.add_install_script('chmod', 'a+r', out)
endforeach

