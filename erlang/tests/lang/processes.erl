-module(processes).
-export([main/0]).

echo() ->
    receive
        {From, Msg} -> From ! {self(), Msg}, echo();
        stop -> ok
    end.

counter(N) ->
    receive
        {inc, K} -> counter(N + K);
        {get, From} -> From ! {count, N}, counter(N)
    end.

main() ->
    Pid = spawn(fun echo/0),
    Pid ! {self(), hello},
    receive {Pid, M} -> io:format("echo: ~p~n", [M]) end,
    Pid ! stop,
    C = spawn(fun() -> counter(0) end),
    [C ! {inc, K} || K <- lists:seq(1, 10)],
    C ! {get, self()},
    receive {count, N} -> io:format("count: ~p~n", [N]) end,
    %% selective receive: take b first, then a
    self() ! a,
    self() ! b,
    receive b -> io:format("got b~n") end,
    receive a -> io:format("got a~n") end,
    %% timeout
    receive never -> ok after 10 -> io:format("timeout~n") end,
    %% monitors
    {P2, Ref} = spawn_monitor(fun() -> exit(boom) end),
    receive {'DOWN', Ref, process, P2, Reason} -> io:format("down: ~p~n", [Reason]) end,
    P3 = spawn(fun() -> ok end),
    Ref3 = monitor(process, P3),
    receive {'DOWN', Ref3, process, _, R3} -> io:format("down: ~p~n", [R3]) end,
    {P4, Ref4} = spawn_monitor(fun() -> error({bad, thing}) end),
    receive {'DOWN', Ref4, process, P4, {R4, _}} -> io:format("down: ~p~n", [R4]) end,
    %% many processes
    Parent = self(),
    Pids = [spawn(fun() -> Parent ! {done, I * I} end) || I <- lists:seq(1, 20)],
    Results = [receive {done, V} -> V end || _ <- Pids],
    io:format("~p~n", [lists:sum(Results)]),
    %% registered names
    register(counter, spawn(fun() -> counter(5) end)),
    counter ! {inc, 1},
    counter ! {get, self()},
    receive {count, N2} -> io:format("registered: ~p~n", [N2]) end,
    io:format("~p~n", [is_pid(self())]),
    ok.
