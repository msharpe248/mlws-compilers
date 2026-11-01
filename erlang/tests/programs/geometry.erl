-module(geometry).
-export([area/1]).

area({circle, R}) -> math:pi() * R * R;
area({square, S}) -> S * S.
