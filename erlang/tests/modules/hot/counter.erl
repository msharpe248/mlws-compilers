-module(counter).
-export([start/0, start_local/0, loop/1, version/0]).

start() -> spawn(fun() -> loop(0) end).
start_local() -> spawn(fun() -> local(0) end).

version() -> 1.

loop(N) ->
    receive
        {get, From} -> From ! {count, N, version()}, counter:loop(N + 1);
        stop -> ok
    end.

local(N) ->
    receive
        {get, From} -> From ! {count, N, version()}, local(N + 1);
        stop -> ok
    end.
