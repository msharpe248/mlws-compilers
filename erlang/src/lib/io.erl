%% io for mlws-vm-erlc: output to standard output (or standard_error).
-module(io).
-export([format/1, format/2, format/3, fwrite/1, fwrite/2, fwrite/3, put_chars/1,
         put_chars/2, nl/0, nl/1, write/1, write/2, get_line/1, get_line/2, columns/0,
         rows/0]).

format(Fmt) -> format(Fmt, []).
format(Fmt, Args) -> put_chars(io_lib:format(Fmt, Args)).
format(Dev, Fmt, Args) -> put_chars(Dev, io_lib:format(Fmt, Args)).

fwrite(Fmt) -> format(Fmt, []).
fwrite(Fmt, Args) -> format(Fmt, Args).
fwrite(Dev, Fmt, Args) -> format(Dev, Fmt, Args).

put_chars(Chars) -> erlang:put_chars(Chars).

put_chars(standard_error, Chars) -> '$rt':put_chars_err(Chars);
put_chars(_, Chars) -> erlang:put_chars(Chars).

nl() -> put_chars("\n").
nl(Dev) -> put_chars(Dev, "\n").

write(T) -> put_chars(io_lib:write(T)).
write(Dev, T) -> put_chars(Dev, io_lib:write(T)).

get_line(Prompt) -> get_line(standard_io, Prompt).
get_line(_Dev, Prompt) ->
    put_chars(Prompt),
    '$rt':get_line().

columns() -> {error, enotsup}.
rows() -> {error, enotsup}.
