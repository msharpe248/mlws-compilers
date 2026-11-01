-module(wordfreq).
-export([main/0]).

text() ->
    "the quick brown fox jumps over the lazy dog the dog barks "
    "and the fox runs away quick quick".

main() ->
    Words = string:tokens(text(), " "),
    Freq = lists:foldl(fun(W, M) -> maps:update_with(W, fun(C) -> C + 1 end, 1, M) end, #{}, Words),
    Sorted = lists:sort(fun({A, X}, {B, Y}) -> {Y, A} =< {X, B} end, maps:to_list(Freq)),
    [io:format("~-8s ~B~n", [W, C]) || {W, C} <- lists:sublist(Sorted, 5)],
    io:format("~p words, ~p distinct~n", [length(Words), maps:size(Freq)]).
