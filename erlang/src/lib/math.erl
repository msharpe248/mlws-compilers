%% math for mlws-vm-erlc.
-module(math).
-export([pi/0, sqrt/1, pow/2, sin/1, cos/1, tan/1, asin/1, acos/1, atan/1, atan2/2,
         exp/1, log/1, log2/1, log10/1, floor/1, ceil/1, fmod/2, sinh/1, cosh/1, tanh/1, e/0]).

pi() -> 3.141592653589793.
e() -> 2.718281828459045.
sqrt(X) -> '$rt':fmath(0, X).
sin(X) -> '$rt':fmath(1, X).
cos(X) -> '$rt':fmath(2, X).
tan(X) -> '$rt':fmath(3, X).
asin(X) -> '$rt':fmath(4, X).
acos(X) -> '$rt':fmath(5, X).
atan(X) -> '$rt':fmath(6, X).
exp(X) -> '$rt':fmath(7, X).
log(X) -> '$rt':fmath(8, X).
log2(X) -> '$rt':fmath(9, X).
log10(X) -> '$rt':fmath(10, X).
floor(X) -> float(erlang:floor(X)).
ceil(X) -> float(erlang:ceil(X)).
pow(X, Y) -> '$rt':fmath2(0, X, Y).
atan2(Y, X) -> '$rt':fmath2(1, Y, X).
fmod(X, Y) -> '$rt':fmath2(2, X, Y).
sinh(X) -> (exp(X) - exp(-X)) / 2.
cosh(X) -> (exp(X) + exp(-X)) / 2.
tanh(X) -> sinh(X) / cosh(X).
