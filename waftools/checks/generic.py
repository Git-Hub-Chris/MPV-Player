import os
from inflectors import DependencyInflector

__all__ = [
    "check_pkg_config", "check_cc", "check_statement", "check_libs",
    "check_headers", "compose_checks", "check_true", "any_version",
    "load_fragment", "check_stub", "check_ctx_vars"]

any_version = None

def even(n):
    return n % 2 == 0

def __define_options__(dependency_identifier):
    return DependencyInflector(dependency_identifier).define_dict()

def __merge_options__(dependency_identifier, *args):
    options_accu = DependencyInflector(dependency_identifier).storage_dict()
    options_accu['mandatory'] = False
    [options_accu.update(arg) for arg in args if arg]
    return options_accu


def check_libs(libs, function):
    libs = [None] + libs
    def fn(ctx, dependency_identifier):
        for lib in libs:
            kwargs = lib and {'lib': lib} or {}
            if function(ctx, dependency_identifier, **kwargs):
                return True
        return False
    return fn

def check_statement(header, statement, **kw_ext):
    def fn(ctx, dependency_identifier, **kw):
        fragment = """
            #include <{0}>
            int main(int argc, char **argv)
            {{ {1}; return 0; }} """.format(header, statement)
        opts = __merge_options__(dependency_identifier,
                                 {'fragment':fragment},
                                 __define_options__(dependency_identifier),
                                 kw_ext, kw)
        return ctx.check_cc(**opts)
    return fn

def check_cc(**kw_ext):
    def fn(ctx, dependency_identifier, **kw):
        options = __merge_options__(dependency_identifier,
                                    __define_options__(dependency_identifier),
                                    kw_ext, kw)
        return ctx.check_cc(**options)
    return fn

def check_pkg_config(*args, **kw_ext):
    def fn(ctx, dependency_identifier, **kw):
        argsl     = list(args)
        packages  = [el for (i, el) in enumerate(args) if even(i)]
        sargs     = [i for i in args if i] # remove None
        pkgc_args = ["--libs", "--cflags"]
        if ctx.dependency_satisfied('static-build'):
            pkgc_args += ["--static"]

        defaults = {
            'path':    ctx.env.PKG_CONFIG,
            'package': " ".join(packages),
            'args':    sargs + pkgc_args }
        opts = __merge_options__(dependency_identifier, defaults, kw_ext, kw)
        if ctx.check_cfg(**opts):
            return True
        else:
            defkey = DependencyInflector(dependency_identifier).define_key()
            ctx.undefine(defkey)
            return False
    return fn

def check_headers(*headers):
    def undef_others(ctx, headers, found):
        not_found_hs = set(headers) - set([found])
        for not_found_h in not_found_hs:
            defkey = DependencyInflector(not_found_h).define_key()
            ctx.undefine(defkey)

    def fn(ctx, dependency_identifier):
        for header in headers:
            defaults = {'header_name': header, 'features': 'c cprogram'}
            options  = __merge_options__(dependency_identifier, defaults)
            if ctx.check(**options):
                undef_others(ctx, headers, header)
                defkey = DependencyInflector(dependency_identifier).define_key()
                ctx.define(defkey, 1)
                return True
        undef_others(ctx, headers, None)
        return False
    return fn

def check_true(ctx, dependency_identifier):
    defkey = DependencyInflector(dependency_identifier).define_key()
    ctx.define(defkey, 1)
    return True

def check_ctx_vars(*variables):
    def fn(ctx, dependency_identifier):
        missing = []
        for variable in variables:
            if variable not in ctx.env:
                missing.append(variable)

        if any(missing):
            ctx.add_optional_message(dependency_identifier,
                'missing {0}'.format(', '.join(missing)))
            return False
        else:
            return True

    return fn

def check_stub(ctx, dependency_identifier):
    defkey = DependencyInflector(dependency_identifier).define_key()
    ctx.undefine(defkey)
    return False

def compose_checks(*checks):
    def fn(ctx, dependency_identifier):
        return all([check(ctx, dependency_identifier) for check in checks])
    return fn

def load_fragment(fragment):
    file_path = os.path.join(os.path.dirname(__file__), '..', 'fragments',
                             fragment)
    fp = open(file_path,"r")
    fragment_code = fp.read()
    fp.close()
    return fragment_code
