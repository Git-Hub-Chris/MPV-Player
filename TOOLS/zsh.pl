#!/usr/bin/perl

# Generate ZSH completion

use strict;
use warnings;

my $mpv = $ARGV[0] || 'mpv';

my @opts = parse_opts("$mpv --list-options", '^ (\-\-[^\s\*]*)\*?\s*(.*)', 1);

my @ao = parse_opts("$mpv --ao=help", '^  ([^\s\:]*)\s*: (.*)');
my @vo = parse_opts("$mpv --vo=help", '^  ([^\s\:]*)\s*: (.*)');

my @af = parse_opts("$mpv --af=help", '^  ([^\s\:]*)\s*: (.*)');
my @vf = parse_opts("$mpv --vf=help", '^  ([^\s\:]*)\s*: (.*)');

my @protos = parse_opts("$mpv --list-protocols", '^ ([^\s]*)');

my ($opts_str, $ao_str, $vo_str, $af_str, $vf_str, $protos_str);

$opts_str .= qq{  '$_' \\\n} foreach (@opts);
chomp $opts_str;

$ao_str .= qq{      '$_' \\\n} foreach (@ao);
chomp $ao_str;

$vo_str .= qq{      '$_' \\\n} foreach (@vo);
chomp $vo_str;

$af_str .= qq{      '$_' \\\n} foreach (@af);
chomp $af_str;

$vf_str .= qq{      '$_' \\\n} foreach (@vf);
chomp $vf_str;

$protos_str .= qq{$_ } foreach (@protos);
chomp $protos_str;

my $profile_comp = <<'EOS';
      local -a profiles
      local current
      for current in "${(@f)$($words[1] --profile=help)}"; do
        current=${current//\*/\\\*}
        current=${current//\:/\\\:}
        current=${current//\[/\\\[}
        current=${current//\]/\\\]}
        if [[ $current =~ $'\t'([^$'\t']*)$'\t'(.*) ]]; then
          if [[ -n $match[2] ]]; then
            current="$match[1][$match[2]]"
          else
            current="$match[1]"
          fi
          profiles=($profiles $current)
        fi
      done
      if [[ $state == profile ]]; then
        # For --show-profile, only one allowed
        if (( ${#profiles} > 0 )); then
          _values 'profile' $profiles && rc=0
        fi
      else
        # For --profile, multiple allowed
        profiles=($profiles 'help[list profiles]')
        _values -s , 'profile(s)' $profiles && rc=0
      fi
EOS
chomp $profile_comp;

my $tmpl = <<"EOS";
#compdef mpv

# mpv zsh completion

local curcontext="\$curcontext" state state_descr line
typeset -A opt_args

local rc=1

_arguments -C -S \\
$opts_str
  '*:files:->mfiles' && rc=0

case \$state in
  ao)
    local -a values
    values=(
$ao_str
    )

    _describe -t values 'audio outputs' values && rc=0
  ;;

  vo)
    local -a values
    values=(
$vo_str
    )

    _describe -t values 'video outputs' values && rc=0
  ;;

  af)
    local -a values
    values=(
$af_str
    )

    _describe -t values 'audio filters' values && rc=0
  ;;

  vf)
    local -a values
    values=(
$vf_str
    )

    _describe -t values 'video filters' values && rc=0
  ;;

  profile|profiles)
$profile_comp
  ;;

  mfiles)
    local expl
    _tags files urls
    while _tags; do
      _requested files expl 'media file' _files -g \\
         "*.(#i)(asf|asx|avi|flac|flv|m1v|m2p|m2v|m4v|mjpg|mka|mkv|mov|mp3|mp4|mpe|mpeg|mpg|ogg|ogm|ogv|qt|rm|ts|vob|wav|webm|wma|wmv)(-.)" && rc=0
      if _requested urls; then
        while _next_label urls expl URL; do
          _urls "\$expl[@]" && rc=0
          compadd -S '' "\$expl[@]" $protos_str && rc=0
        done
      fi
      (( rc )) || return 0
    done
  ;;
esac

return rc
EOS

print $tmpl;

sub parse_opts {
    my ($cmd, $regex, $parsing_main_options) = @_;

    my @list;
    my @lines = split /\n/, `$cmd`;

    foreach my $line (@lines) {
        if ($line !~ /^$regex/) {
            next;
        }

        my $entry = $1;

        if ($parsing_main_options) {
            $entry .= '=-';
        }

        if (defined $2) {
            my $desc = $2;
            $desc =~ s/\:/\\:/g;

            $entry .= ':' . $desc;
        }

        if ($parsing_main_options) {
            $entry .= ':';

            $entry .= '->ao' if ($1 eq '--ao');
            $entry .= '->vo' if ($1 eq '--vo');
            $entry .= '->af' if ($1 eq '--af');
            $entry .= '->vf' if ($1 eq '--vf');
            $entry .= '->profiles' if ($1 eq '--profile');
            $entry .= '->profile' if ($1 eq '--show-profile');
        }

        push @list, $entry
    }

    if ($parsing_main_options) {
        @list = sort {
            $a =~ /(.*?)\:/; my $ma = $1;
            $b =~ /(.*?)\:/; my $mb = $1;

            length($mb) <=> length($ma)
        } @list;
    }

    return @list;
}
