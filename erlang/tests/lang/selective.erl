%% Selective receive: messages skipped are kept, in order, for later receives.
-module(selective).
-export([main/0]).
main() ->
    S = self(),
    P = spawn(fun() -> receive go -> ok end, [S ! {n, I} || I <- [1,2,3,4,5]], S ! last end),
    Ref = monitor(process, P),
    P ! go,
    receive last -> ok end,
    R1 = receive {n, 3} -> three end,
    R2 = receive {n, X} when X > 3 -> X end,
    R3 = receive {n, 9} -> nine after 10 -> timeout end,
    R4 = receive {'DOWN', Ref, process, P, Why} -> Why end,
    R5 = [receive {n, Y} -> Y end || _ <- [1,2,3]],
    S ! a, S ! b,
    R6 = receive b -> b end,
    R7 = receive M -> M after 0 -> none end,
    R8 = receive M2 -> M2 after 0 -> none end,
    io:format("~p~n", [[R1, R2, R3, R4, R5, R6, R7, R8]]).
