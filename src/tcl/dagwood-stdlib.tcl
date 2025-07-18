proc flatten-once {args} {
    set out {}
    foreach a $args {
	foreach item $a {
	    lappend out $item
	}
    }
    return $out
}

proc project {name} {
    define_project $name
    uplevel 1 [list set current_project_name $name]
}

proc global_description {str} {
    set raw [string trim $str]
    set desc [subst $raw]
    project_set_description $::current_project_name $desc
}

proc global_shell {str} {
    set raw [string trim $str]
    set default_shell [subst $raw]
    project_set_default_shell $::current_project_name $default_shell
}

proc global_always_run {flag} {
    project_set_always_run $::current_project_name $flag
}

proc task {name body} {
    define_task $name
    upvar 0 $name task_ref

    uplevel 1 [list set current_task_name $name]
    uplevel 1 $body
    uplevel 1 [list unset current_task_name]
}

proc description {str} {
    set raw [string trim $str]
    set desc [uplevel 1 [list subst $raw]]
    uplevel 1 [list set description $desc]
    task_set_description $::current_task_name $desc
}

proc shell {str} {
    set raw [string trim $str]
    set s [uplevel 1 [list subst $raw]]
    uplevel 1 [list set shell $s]
    task_set_shell $::current_task_name $s
}

proc timeout {n} {
    uplevel 1 [list set timeout $n]
    task_set_timeout $::current_task_name $n
}

proc always_run {flag} {
    uplevel 1 [list set always_run $flag]
    task_set_always_run $::current_task_name $flag
}

proc run {body} {
    set raw [string trim $body]
    set cmd [uplevel 1 [list subst $raw]]
    uplevel 1 [list set run $cmd]
    task_set_run $::current_task_name $cmd
}

proc inputs {args} {
    set flat [flatten-once {*}$args]
    uplevel 1 [list set inputs $flat]
    task_set_inputs $::current_task_name {*}$flat
}

proc outputs {args} {
    set flat [flatten-once {*}$args]
    uplevel 1 [list set outputs $flat]
    task_set_outputs $::current_task_name {*}$flat
}

proc depends_on {args} {
    set flat [flatten-once {*}$args]
    uplevel 1 [list set depends_on $flat]
    task_set_depends_on $::current_task_name {*}$flat
}


proc execute-task {tsk} {
    puts "[dagwood] running task: ${tsk.name}"

    if ([info exists tsk(timeout)]) {
	set cmd [format "timeout %s sh -c %s" $tsk(timeout) [list $tsk(command)]]
    } else {
	set cmd [format "sh -c %s" [list $tsk(command)]]
    }
    
    set code [catch {
	set output [exec {*}$cmd]
    } err]

    if ($code == 0) {
	puts "[dagwood] success"
    } else {
	puts "[dagwood] failed: $err"
	return -code error "Task failed: $tsk(name)"
    }
}

proc unique {lst} {
    return [lsort -unique $lst]
}

proc unique-glob {args} {
    set files {}
    foreach pattern $args {
	foreach f [glob $pattern] {
	    lappend files $f
	}
    }

    return [unique $files]
}

proc git-sha {} {
    return [string trim [exec git rev-parse HEAD]]
}

proc timestamp {} {
    return [clock format [clock seconds] -format "%Y%m%d-%H%M%S"]
}

proc read-file {path} {
    set f [open $path r]
    set data [read $f]
    close $f
    return $data
}

proc mkdir-p {path} {
    file mkdir $path
}

proc clean {paths} {
    foreach p $paths {
	if {[file exists $p]} {
	    file delete -force $p
	}
    }
}

# TODO
proc hash-file {path} {
    return [exec sha256sum $path | cut -d " " -f 1]
}
