def review(code):
    violations = 0
    violations += 1 if "and" in code else 0
    violations += 1 if "AbstractFactory" in code else 0
    violations += 1 if code.count("try") > 3 else 0
    violations += 1 if len(code.splitlines()) > 30 else 0
    violations += 1 if "import" not in code else 0
    violations += 1 if "def" not in code else 0
    return violations

print(review("""
def setup_app():
    db = connect()
    cache = redis()
    routes = load_routes()
    print("ready")
"""))
