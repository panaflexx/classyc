" ClassyC filetype detection
" Install: put `classyc/vim` on the runtimepath, e.g. in ~/.vimrc:
"   set runtimepath+=/path/to/classyc/vim
" or packpath:
"   ln -s /path/to/classyc/vim ~/.vim/pack/classyc/start/classyc

au BufRead,BufNewFile *.cy setfiletype cy
