#!/bin/zsh

script_dir="${0:A:h}"

if (( $# == 0 )); then
  print "Drag an EPUB file or books folder here, then press Return:"
  read -r target
  target="${target#\"}"
  target="${target%\"}"
  set -- "$target"
fi

python3 "$script_dir/generate_epub_thumbnails.py" "$@"
status=$?
print
read "?Press Return to close..."
exit $status
