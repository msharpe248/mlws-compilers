-module(helper).
-export([double/1, twice_back/1, tell/2]).

double(X) -> 3 * X.

twice_back(X) -> main:back(main:back(X)).

tell(P, M) -> P ! {told, M}.
