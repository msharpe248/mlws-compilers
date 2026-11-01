%% unicode for mlws-vm-erlc: UTF-8 only.
-module(unicode).
-export([characters_to_binary/1, characters_to_binary/2, characters_to_binary/3,
         characters_to_list/1, characters_to_list/2]).

characters_to_binary(L) -> '$rt':utf8_bin(L).
characters_to_binary(L, _) -> characters_to_binary(L).
characters_to_binary(L, _, _) -> characters_to_binary(L).

characters_to_list(B) when is_binary(B) -> atom_to_list('$rt':bin_str(B));
characters_to_list(L) when is_list(L) ->
    lists:flatten([case X of
                       B when is_binary(B) -> characters_to_list(B);
                       _ -> X
                   end || X <- lists:flatten(L)]).
characters_to_list(L, _) -> characters_to_list(L).
