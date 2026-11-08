-module(file).
-export([read_file/1, write_file/2]).

%% Whole files, on MLWSVM's fs.read_file and fs.write_file. A name is a
%% string, a binary or an atom.

read_file(Name) -> '$rt':file_read(Name).

write_file(Name, Data) -> '$rt':file_write(Name, Data).
