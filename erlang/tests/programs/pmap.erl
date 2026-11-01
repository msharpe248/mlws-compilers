%% Parallel map over processes, and a supervisor-ish restart loop.
-module(pmap).
-export([main/0]).

pmap(F, L) ->
    Parent = self(),
    Refs = [begin
                Ref = make_ref(),
                spawn(fun() -> Parent ! {Ref, catch F(X)} end),
                Ref
            end || X <- L],
    [receive {Ref, R} -> R end || Ref <- Refs].

fib(0) -> 0;
fib(1) -> 1;
fib(N) -> fib(N - 1) + fib(N - 2).

worker(N) ->
    case N rem 3 of
        0 -> exit({crash, N});
        _ -> N * 10
    end.

supervise(N, Max) when N > Max -> ok;
supervise(N, Max) ->
    {Pid, Ref} = spawn_monitor(fun() -> exit(worker(N)) end),
    receive
        {'DOWN', Ref, process, Pid, {crash, K}} -> io:format("worker ~p crashed~n", [K]);
        {'DOWN', Ref, process, Pid, R} -> io:format("worker ~p: ~p~n", [N, R])
    end,
    supervise(N + 1, Max).

main() ->
    io:format("~p~n", [pmap(fun fib/1, lists:seq(10, 25))]),
    io:format("~p~n", [[case R of {'EXIT', {Why, _Stack}} -> Why; _ -> R end
                        || R <- pmap(fun(X) -> 10 div X end, [1, 2, 0, 5])]]),
    supervise(1, 6).
