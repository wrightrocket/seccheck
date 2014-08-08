#
# functions used by all modules
#
# param: program name
function set_tmpdir () {
    TMPDIR=`/bin/mktemp -d /tmp/$1.XXXXXX` || exit 1
}

function run_sysconfig_seccheck () {
    test -e /etc/sysconfig/seccheck && . /etc/sysconfig/seccheck
}

function syntax () {
    /bin/echo "Syntax: $0 "'daily|weekly|monthly'
    exit 1
}

function set_mailer () {
    test -z "$MAILER" && test -x "/usr/sbin/sendmail" && MAILER="/usr/sbin/sendmail"
    test -z "$MAILER" && test -x "/usr/bin/mailx" && MAILER="/usr/bin/mailx"
    test -z "$MAILER" && test -x "/usr/lib/sendmail" && MAILER="/usr/lib/sendmail"
    test -z "$MAILER" && MAILER="mail"
    test -n "$MAILER" && export MAILER
}

function create_secdir () {
    if [ ! -d "$SEC_VAR" ]; then
        rm -rf "$SEC_VAR"
        mkdir "$SEC_VAR" || exit 1
    fi

    if [ ! -d "$SEC_DATA" ]; then
        rm -rf "$SEC_DATA"
        mkdir "$SEC_DATA" || exit 1
    fi
}

# param a username
function guessable_password_email {
    RET_TMPL=`sed "s/{guessable_account}/$1/" blurbs/guessable_passwd.txt`
    echo $RET_TMPL
}

# those files are needed 
function initialize_secfiles () {
    for i in "$SEC_DATA/rpm-md5" "$SEC_DATA/sbit" "$SEC_DATA/write" "$SEC_DATA/devices" "$SEC_DATA/write-bin"; do
        if [ ! -e "$i" ] ; then
            touch "$i"
        fi
    done
}

# params 
# $1 = directory for checkneverlogin
# $2 = file to receive the redirected output
function check_neverlogin () {
    bin_path = $1
    output_file = $2

    $bin_path/checkneverlogin > "$output_file"
    if [ -s "$output_file" ] ; then
	    printf "\nPlease check and perhaps disable the following unused accounts:\n"
	    cat $output_file
    fi
    rm -f $output_file
}

# use john the ripper to check guessable passwords
# if you pass "quick" as argument it will simple try to find easy 
# guessable passwords. otherwise it will use a dictionary
function check_guessable_passwords () {
    if type -p john >/dev/null && type -p unshadow >/dev/null ; then
        # Copy passwd file. Use unique name to avoid races when john takes very long
        SEC_PASSWD=$SEC_VAR/passwd.$$
        OUT="$TMPDIR/security.out" # random name please
        echo -e '\nComplete list of user accounts with guessable passwords:'
        unshadow /etc/passwd /etc/shadow > $SEC_PASSWD
        if [ "$1" != "quick" ]; then
            nice -n 1 john -single "$SEC_PASSWD" 1> /dev/null 2>&1
            nice -n 1 john -rules -w:$SEC_VAR/dict "$SEC_PASSWD" 1> /dev/null 2>&1
        fi

        john -show "$SEC_PASSWD" | sed -n 's/:.*//p' > "$OUT"

        if [ "$1" != "quick" ]; then
            if [ -s "$OUT" ] ; then
                for i in `cat "$OUT"`; do
                    $MAILER guessable_password_email "$i"  # set mailer
                done
            end
        fi
        cat $OUT
        /bin/rm -f $SEC_VAR/passwd
        /bin/rm -f $OUT
    fi

}

# param mount points
function check_suid_sgid () {
    ( nice -n 1 find $1 -mount \( -perm -04000 -o -perm -02000 \) -type f | sort | \
        xargs --no-run-if-empty ls -cdl --time-style=long-iso -- > "$SEC_DATA/sbit.new" ) 2> /dev/null
    diff -uw "$SEC_DATA/sbit" "$SEC_DATA/sbit.new" | \
        egrep -v '^\+\+\+ |^--- |^$|^@@' | sed 's/^[+-]/& /' > "$OUT"
    if [ -s "$OUT" ] ; then
        printf "\nThe following files are suid/sgid:\n"
        cat "$OUT"
    fi
    mv "$SEC_DATA/sbit.new" "$SEC_DATA/sbit"
}

# param mount points
function check_writable_executable () {
    ( nice -n 1 find $1 -mount \( -perm -30 -o -perm -3 \) -type f | sort | \
        xargs --no-run-if-empty ls -cdl --time-style=long-iso -- > "$SEC_DATA/write-bin.new" ) 2> /dev/null
    diff -uw "$SEC_DATA/write-bin" "$SEC_DATA/write-bin.new" | \
        egrep -v '^\+\+\+ |^--- |^$|^@@' | sed 's/^[+-]/& /' > "$OUT"
    if [ -s "$OUT" ] ; then
        printf "\nThe following program executables are group/world writeable:\n"
        cat "$OUT"
    fi
    mv "$SEC_DATA/write-bin.new" "$SEC_DATA/write-bin"
}

# param mount point
function check_world_writable () {
    ( nice -n 1 find $1 -mount -perm -2 \( -type f -o -type d \) -not -perm -01000 | sort > "$SEC_DATA/write.new" ) 2> /dev/null
    diff -uw "$SEC_DATA/write" "$SEC_DATA/write.new" | \
        egrep -v '^\+\+\+ |^--- |^$|^@@' | sed 's/^[+-]/& /' > "$OUT"
    if [ -s "$OUT" ] ; then
        printf "\nThe following files/directories are world writeable and not sticky:\n"
        cat "$OUT"
    fi
    mv "$SEC_DATA/write.new" "$SEC_DATA/write"
}

# param mount point
function check_new_devices () {
    # warning: bug #51004 ls output depends on root's locale and may be less
    # then 10 tokens!
    ( nice -n 1 find $1 -mount -type c -or -type b | xargs --no-run-if-empty ls -cdl --time-style=long-iso -- | \
    awk '{print $1 " \t" $3 " \t" $4 " \t" $5 " \t" $6 " \t" $9}' | sort +5 \
    > "$SEC_DATA/devices.new" ) 2> /dev/null
    diff -uw "$SEC_DATA/devices" "$SEC_DATA/devices.new" | \
    egrep -v '^\+\+\+ |^--- |^$|^@@' | sed 's/^[+-]/& /' > "$OUT"
    if [ -s "$OUT" ] ; then
        printf "\nThe following devices were added:\n"
        cat "$OUT"
    fi
    mv "$SEC_DATA/devices.new" "$SEC_DATA/devices"

}

function check_md5 () {
    nice -n 1 rpm -Va 2> /dev/null | grep '^5' > "$SEC_DATA/rpm-md5.new"
    diff -uw "$SEC_DATA/rpm-md5" "$SEC_DATA/rpm-md5.new" | \
        egrep -v '^\+\+\+ |^--- |^$|^@@' | sed 's/^[+-]/& /' > "$OUT"
    if [ -s "$OUT" ] ; then
        printf "\nThe following programs have got a different md5 checksum since last week:\n"
        cat "$OUT"
    fi
    mv "$SEC_DATA/rpm-md5.new" "$SEC_DATA/rpm-md5"
}
