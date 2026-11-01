-module(stack).
-export([new/0, push/2, pop/1, size/1]).

new() -> [].
push(X, S) -> [X | S].
pop([X | S]) -> {X, S}.
size(S) -> length(S).
