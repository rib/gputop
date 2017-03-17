

class Codegen:

    _file = None
    _indent = 0

    endl="\n"
    use_tabs = False

    def __init__(self, filename = None):
        if filename != None:
            self._file = open(filename, 'w')

    def __call__(self, *args):
        if self._file:
            code = ' '.join(map(str, args))
            for line in code.splitlines():
                indent = ''.rjust(self._indent)

                if self.use_tabs:
                    indent = indent.replace("        ", "\t")

                text = indent + line
                self._file.write(text.rstrip() + self.endl)

    #without indenting or new lines
    def frag(self, *args):
        code = ' '.join(map(str, args))
        self._file.write(code)

    def indent(self, n):
        self._indent = self._indent + n
    def outdent(self, n):
        self._indent = self._indent - n

