# usage: python3 -I i18n_check.py FILE.py  -> checks every dict in FILE against i18n_data.EN
import sys, re, importlib.util, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import i18n_data
spec = importlib.util.spec_from_file_location("part", sys.argv[1]); m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
ok = True
for name in dir(m):
    d = getattr(m, name)
    if name.isupper() and isinstance(d, dict) and name != "EN":
        miss = set(i18n_data.EN) - set(d); extra = set(d) - set(i18n_data.EN)
        ph = [k for k in i18n_data.EN if k in d and sorted(re.findall(r"\{\d\}", i18n_data.EN[k])) != sorted(re.findall(r"\{\d\}", d[k]))]
        empty = [k for k in d if not str(d[k]).strip()]
        print(name, len(d), "missing", sorted(miss), "extra", sorted(extra), "placeholder-mismatch", ph, "empty", empty)
        ok = ok and not (miss or extra or ph or empty)
print("OK" if ok else "PROBLEMS"); sys.exit(0 if ok else 1)
