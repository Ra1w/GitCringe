#!/usr/bin/env bash

_gitcringe_completions() {
    local cur command

    cur="${COMP_WORDS[COMP_CWORD]}"
    
    local commands="init add commit log status exec help switch branch show merge diff squash"

    if [[ ${COMP_CWORD} -eq 1 ]]; then
        mapfile -t COMPREPLY < <(compgen -W "${commands}" -- "${cur}")
        return 0
    fi

    command="${COMP_WORDS[1]}"

    _get_branches() {
        gitcringe branch 2>/dev/null | sed 's/\x1b\[[0-9;]*m//g' | awk '{print $NF}'
    }

    case "${command}" in
        add)
            mapfile -t COMPREPLY < <(compgen -f -- "${cur}")
            ;;
        commit)
            if [[ "${cur}" == -* ]]; then
                mapfile -t COMPREPLY < <(compgen -W "-m -am" -- "${cur}")
            fi
            ;;
        log)
            if [[ "${cur}" == -* ]]; then
                mapfile -t COMPREPLY < <(compgen -W "--oneline --all" -- "${cur}")
            else
                local branches
                branches=$(_get_branches)
                mapfile -t COMPREPLY < <(compgen -W "HEAD ${branches}" -- "${cur}")
            fi
            ;;
        show)
            if [[ "${cur}" == -* ]]; then
                compopt -o nospace
                mapfile -t COMPREPLY < <(compgen -W "--format=" -- "${cur}")
            else
                local branches
                branches=$(_get_branches)
                mapfile -t COMPREPLY < <(compgen -W "HEAD ${branches}" -- "${cur}")
            fi
            ;;
        switch|merge|diff|squash|branch)
            local branches
            branches=$(_get_branches)
            mapfile -t COMPREPLY < <(compgen -W "HEAD ${branches}" -- "${cur}")
            ;;
        *)
            COMPREPLY=()
            ;;
    esac
    return 0
}

complete -o default -F _gitcringe_completions gitcringe