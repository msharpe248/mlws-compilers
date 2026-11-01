-module(primes).
-export([main/0]).

sieve([]) -> [];
sieve([P | Xs]) -> [P | sieve([X || X <- Xs, X rem P =/= 0])].

queens(N) -> queens(N, N).
queens(0, _) -> [[]];
queens(K, N) -> [[Q | Qs] || Qs <- queens(K - 1, N), Q <- lists:seq(1, N), safe(Q, Qs, 1)].

safe(_, [], _) -> true;
safe(Q, [Q1 | Qs], D) -> Q =/= Q1 andalso abs(Q - Q1) =/= D andalso safe(Q, Qs, D + 1).

main() ->
    Ps = sieve(lists:seq(2, 200)),
    io:format("~p primes, last ~p~n", [length(Ps), lists:last(Ps)]),
    io:format("~p~n", [Ps]),
    io:format("8 queens: ~p solutions~n", [length(queens(8))]),
    io:format("first: ~p~n", [hd(queens(8))]).
