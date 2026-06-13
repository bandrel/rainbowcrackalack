"""Tests for configs.py."""
import os, sys, unittest
THIS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, THIS_DIR)
from configs import CONFIGS, gen_argv

class TestConfigs(unittest.TestCase):
    def test_all_paths_present(self):
        self.assertEqual(
            set(CONFIGS),
            {"netntlmv1_7", "ntlm8", "ntlm9", "ntlm10", "md5_8", "md5_9"},
        )

    def test_each_has_args_and_kernel(self):
        for name, c in CONFIGS.items():
            self.assertIn("gen_args", c, name)
            self.assertIn("gen_kernel", c, name)
            self.assertIsInstance(c["gen_args"], list, name)

    def test_gen_argv_appends_bench(self):
        argv = gen_argv("ntlm8", num_chains=1000)
        self.assertEqual(
            argv,
            ["ntlm", "ascii-32-95", "8", "8", "0", "422000", "1000", "-bench"],
        )

    def test_netntlmv1_kernel_name(self):
        self.assertEqual(CONFIGS["netntlmv1_7"]["gen_kernel"], "crackalack_netntlmv1_7")

if __name__ == "__main__":
    unittest.main()
