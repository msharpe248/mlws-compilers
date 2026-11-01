-module(links).
-export([main/0]).

main() ->
    process_flag(trap_exit, true),
    P = spawn_link(fun() -> exit(oops) end),
    receive {'EXIT', P, R} -> io:format("exit: ~p~n", [R]) end,
    P2 = spawn_link(fun() -> ok end),
    receive {'EXIT', P2, R2} -> io:format("exit: ~p~n", [R2]) end,
    Self = self(),
    Worker = spawn(fun() -> receive {ping, From} -> From ! pong end end),
    Worker ! {ping, Self},
    receive pong -> io:format("pong~n") after 1000 -> io:format("no pong~n") end,
    Ref = make_ref(),
    self() ! {Ref, hello},
    receive {Ref, Msg} -> io:format("ref msg ~p~n", [Msg]) end,
    io:format("~p~n", [is_reference(Ref)]),
    T = erlang:monotonic_time(millisecond),
    timer:sleep(20),
    io:format("slept ~p~n", [erlang:monotonic_time(millisecond) - T >= 20]),
    put(key, value),
    io:format("~p ~p~n", [get(key), get(missing)]),
    io:format("~p~n", [whereis(nobody)]),
    ok.
