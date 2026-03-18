## 🪨 Flint (Fast Little INterpreted Tool)

Scripting language for shell automation

### usage

```bash
$ make # requires clang -std=gnu23
$ ./flint script.ft
```

basic syntax guide, very familiar

```js
const name = 'flint';
let nums = [1, 2, 3];
let config = { host: 'localhost', port: 8080 };

println(`${name} has ${len(nums)} items`);
println(config.host);
```

shell commands with `` $`...` `` syntax, `exec()`, `pipe()`, and `fetch()`:

```js
println($`whoami`);

const csv = 'name,age\nalice,30\nbob,25';
const names = pipe(csv, "awk -F, 'NR>1 {print $1}'");
```

### builtins

| function                                     | description                                                      |
| -------------------------------------------- | ---------------------------------------------------------------- |
| `print(...)` / `println(...)`                | print to stdout                                                  |
| `eprintln(...)`                              | print to stderr                                                  |
| `len(v)`                                     | length of string, array, or object                               |
| `type(v)`                                    | returns `"num"`, `"str"`, `"bool"`, `"nil"`, `"arr"`, or `"obj"` |
| `str(v)` / `num(v)`                          | type conversion                                                  |
| `trim(s)`                                    | strip whitespace                                                 |
| `substr(s, start, len?)`                     | substring                                                        |
| `index_of(s, needle)`                        | find position, or `-1`                                           |
| `replace(s, old, new)`                       | replace all occurrences                                          |
| `upper(s)` / `lower(s)`                      | case conversion                                                  |
| `push(arr, v)` / `pop(arr)`                  | array mutation                                                   |
| `keys(obj)` / `values(obj)`                  | object introspection                                             |
| `has(obj, key)`                              | check if key exists                                              |
| `delete obj.key`                             | remove key from object                                           |
| `exec(cmd)`                                  | run shell command, return stdout                                 |
| `pipe(data, cmd)`                            | pipe string into shell command                                   |
| `fetch(url)`                                 | HTTP GET via curl                                                |
| `read_file(path)` / `write_file(path, data)` | file I/O                                                         |
| `env(name)` / `setenv(name, val)`            | environment variables                                            |
| `fork()` / `wait()`                          | process control                                                  |
| `exit(code?)`                                | exit                                                             |
