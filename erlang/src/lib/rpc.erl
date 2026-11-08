-module(rpc).
-export([call/4, call/5, cast/4]).

%% rpc on spawn/2: the function runs in a process on Node, which sends the
%% result back.
call(Node, M, F, A) -> call(Node, M, F, A, infinity).

call(Node, M, F, A, Timeout) ->
    Me = self(),
    Ref = make_ref(),
    case catch spawn(Node, fun() -> Me ! {Ref, catch apply(M, F, A)} end) of
        {'EXIT', _} -> {badrpc, nodedown};
        P ->
            MRef = monitor(process, P),
            receive
                {Ref, R} ->
                    demonitor(MRef, [flush]),
                    case R of
                        {'EXIT', Why} -> {badrpc, {'EXIT', Why}};
                        _ -> R
                    end;
                {'DOWN', MRef, process, _, noconnection} -> {badrpc, nodedown};
                {'DOWN', MRef, process, _, Why} -> {badrpc, {'EXIT', Why}}
            after Timeout ->
                demonitor(MRef, [flush]),
                {badrpc, timeout}
            end
    end.

cast(Node, M, F, A) ->
    catch spawn(Node, M, F, A),
    true.
