import unittest
from unittest.mock import Mock
from dependency_vex import annotate, load_assessments, openvex


class AssessmentTests(unittest.TestCase):
    def setUp(self):
        self.component = {"id": "sqlite", "version": "3.54.1",
                          "purl": "pkg:github/sqlite/sqlite@version-3.54.1",
                          "paths": [{"path": "lib/sqlite", "git_object": "a" * 40}]}
        self.guards = {"lib/sqlite": "a" * 40, "cmake": "b" * 40,
                       "CMakeLists.txt": "c" * 40,
                       ".github/workflows/windows-dll.yml": "d" * 40}
        self.a = {"component": "sqlite", "version": "3.54.1",
                  "advisory": "CVE-2026-119586", "status": "not_affected",
                  "justification": "component_not_present",
                  "reviewed": "2026-11-25", "source": "https://sqlite.org/cves.html",
                  "evidence": "CLI excluded", "guards": [
                      {"path": p, "git_object": h} for p, h in self.guards.items()]}
        self.data = {"schema_version": 1, "assessments": [self.a]}
        self.shell = ""
        self.dirty = ""
        self.git = Mock(side_effect=self.fake_git)

    def fake_git(self, *args):
        if args[0] == "rev-parse":
            return self.guards[args[1][5:]]
        if args[0] == "status":
            return " M " + self.dirty if args[-1] == self.dirty else ""
        if args[0] == "ls-files":
            return self.shell
        raise AssertionError(args)

    def evaluate(self):
        return load_assessments(self.data, [self.component], self.git)

    def test_assessed_and_unknown_advisory_preserved(self):
        assessments = self.evaluate()
        report = annotate({"components": [{"id": "sqlite", "advisories":
                         ["CVE-2026-119586", "CVE-2099-1234"]}]}, assessments)
        row = report["components"][0]
        self.assertEqual(len(row["advisories"]), 2)
        self.assertEqual([f["status"] for f in row["findings"]],
                         ["not_affected", "under_investigation"])
        statement = openvex(assessments, [self.component], "f"*40,
                            "2026-11-25T00:00:00Z", "example/addon")["statements"][0]
        self.assertEqual(statement["status"], "not_affected")
        self.assertEqual(statement["justification"], "component_not_present")

    def test_changed_build_requires_review(self):
        self.guards["cmake"] = "e" * 40
        a = next(iter(self.evaluate().values()))
        self.assertEqual(a["effective_status"], "under_investigation")
        statement = openvex(self.evaluate(), [self.component], "f"*40,
                            "2026-11-25T00:00:00Z", "example/addon")["statements"][0]
        self.assertNotIn("justification", statement)

    def test_changed_version_requires_review(self):
        self.component["version"] = "3.55.0"
        self.assertEqual(next(iter(self.evaluate().values()))["effective_status"],
                         "under_investigation")

    def test_dirty_build_requires_review(self):
        self.dirty = "CMakeLists.txt"
        self.assertEqual(next(iter(self.evaluate().values()))["effective_status"],
                         "under_investigation")

    def test_shell_presence_requires_review(self):
        self.shell = "lib/sqlite/shell.c"
        self.assertEqual(next(iter(self.evaluate().values()))["effective_status"],
                         "under_investigation")

    def test_invalid_justification_rejected(self):
        self.a["justification"] = "ignore"
        with self.assertRaises(ValueError):
            self.evaluate()


if __name__ == "__main__":
    unittest.main()
