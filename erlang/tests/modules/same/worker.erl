-module(worker).
-export([start/0, ask/1, kind/1]).

start() -> spawn(fun() -> loop() end).

ask(P) ->
    P ! {ping, self()},
    receive {pong, R} -> R end.

loop() ->
    receive {ping, From} -> From ! {pong, fun() -> ok end()}, loop() end.

kind(pong) -> a_pong;
kind(ping) -> a_ping;
kind(_) -> other.
