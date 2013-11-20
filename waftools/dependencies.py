from waflib.Errors import ConfigurationError, WafError
from waflib.Configure import conf
from waflib.Build import BuildContext
from waflib.Logs import pprint
from inflectors import DependencyInflector

class DependencyError(Exception):
    pass

class Dependency(object):
    def __init__(self, ctx, satisfied_deps, dependency):
        self.ctx = ctx
        self.satisfied_deps = satisfied_deps
        self.identifier, self.desc = dependency['name'], dependency['desc']
        self.attributes = self.__parse_attributes__(dependency)

    def __parse_attributes__(self, dependency):
        if 'os_specific_checks' in dependency:
            all_chks = dependency['os_specific_checks']
            chks = [check for check in all_chks if check in self.satisfied_deps]
            if any(chks):
                return all_chks[chks[0]]
        return dependency

    def check(self):
        self.ctx.start_msg('Checking for {0}'.format(self.desc))

        try:
            self.check_disabled()
            self.check_any_dependencies()
            self.check_dependencies()
            self.check_negative_dependencies()
        except DependencyError:
            # No check was run, since the prerequisites of the dependency are
            # not satisfied. Make sure the define is 'undefined' so that we
            # get a `#define YYY 0` in `config.h`.
            def_key = DependencyInflector(self.identifier).define_key()
            self.ctx.undefine(def_key)
            self.fatal_if_needed()
            return

        self.check_autodetect_func()

    def check_disabled(self):
        if self.enabled_option() == False:
            self.skip()
            raise DependencyError

        if self.enabled_option() == True:
            self.attributes['req'] = True
            self.attributes['fmsg'] = "You manually enabled the feature '{0}', but \
the autodetection check failed.".format(self.identifier)

    def check_any_dependencies(self):
        if 'deps_any' in self.attributes:
            deps = set(self.attributes['deps_any'])
            if len(deps & self.satisfied_deps) == 0:
                self.skip("not found any of {0}".format(", ".join(deps)))
                raise DependencyError

    def check_dependencies(self):
        if 'deps' in self.attributes:
            deps = set(self.attributes['deps'])
            if not deps <= self.satisfied_deps:
                missing_deps = deps - self.satisfied_deps
                self.skip("{0} not found".format(", ".join(missing_deps)))
                raise DependencyError

    def check_negative_dependencies(self):
        if 'deps_neg' in self.attributes:
            deps = set(self.attributes['deps_neg'])
            if deps <= self.satisfied_deps:
                conflicting_deps = deps & self.satisfied_deps
                self.skip("{0} found".format(", ".join(conflicting_deps)), 'CYAN')
                raise DependencyError

    def check_autodetect_func(self):
        if self.attributes['func'](self.ctx, self.identifier):
            self.success(self.identifier)
        else:
            self.fail()
            self.fatal_if_needed()

    def enabled_option(self):
        try:
            return getattr(self.ctx.options, self.enabled_option_repr())
        except AttributeError:
            pass
        return None

    def enabled_option_repr(self):
        return "enable_{0}".format(self.identifier)

    def success(self, depname):
        self.ctx.mark_satisfied(depname)
        self.ctx.end_msg(self.__message__('yes'))

    def fail(self, reason='no'):
        self.ctx.end_msg(self.__message__(reason), 'RED')

    def fatal_if_needed(self):
        if self.attributes.get('req', False):
            raise ConfigurationError(self.attributes['fmsg'])

    def skip(self, reason='disabled', color='YELLOW'):
        self.ctx.end_msg(self.__message__(reason), color)

    def __message__(self, message):
        optional_message = self.ctx.deps_msg.get(self.identifier)
        if optional_message:
            return "{0} ({1})".format(message, optional_message)
        else:
            return message

def configure(ctx):
    def __detect_target_os_dependency__(ctx):
        target = "os-{0}".format(ctx.env.DEST_OS)
        ctx.start_msg('Detected target OS:')
        ctx.end_msg(target)
        ctx.env.satisfied_deps.add(target)

    ctx.deps_msg = {}
    ctx.env['satisfied_deps'] = set()
    __detect_target_os_dependency__(ctx)

@conf
def mark_satisfied(ctx, dependency_identifier):
    ctx.env.satisfied_deps.add(dependency_identifier)

@conf
def add_optional_message(ctx, dependency_identifier, message):
    ctx.deps_msg[dependency_identifier] = message

@conf
def parse_dependencies(ctx, dependencies):
    def __check_dependency__(ctx, dependency):
        Dependency(ctx, ctx.env.satisfied_deps, dependency).check()

    [__check_dependency__(ctx, dependency) for dependency in dependencies]

@conf
def dependency_satisfied(ctx, dependency_identifier):
    return dependency_identifier in ctx.env.satisfied_deps

def filtered_sources(ctx, sources):
    def __source_file__(source):
        if isinstance(source, tuple):
            return source[0]
        else:
            return source

    def __check_filter__(dependency):
        if dependency.find('!') == 0:
            return dependency.lstrip('!') not in ctx.env.satisfied_deps
        else:
            return dependency in ctx.env.satisfied_deps

    def __unpack_and_check_filter__(source):
        try:
            _, dependency = source
            return __check_filter__(dependency)
        except ValueError:
            return True

    return [__source_file__(source) for source in sources \
            if __unpack_and_check_filter__(source)]

def env_fetch(tx):
    def fn(ctx):
        deps = list(ctx.env.satisfied_deps)
        lists = [ctx.env[tx(dep)] for dep in deps if (tx(dep) in ctx.env)]
        return [item for sublist in lists for item in sublist]
    return fn

def dependencies_use(ctx):
    return [DependencyInflector(dep).storage_key() for \
                dep in ctx.env.satisfied_deps]

BuildContext.filtered_sources = filtered_sources
BuildContext.dependencies_use = dependencies_use
BuildContext.dependencies_includes  = env_fetch(lambda x: "INCLUDES_{0}".format(x))
BuildContext.dependency_satisfied = dependency_satisfied
