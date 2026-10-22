"""Verify that the screenshot oracle rejects damaged and incomplete images."""
from pathlib import Path
import importlib.util
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[9]
spec=importlib.util.spec_from_file_location('oracle',ROOT/'arch/x86/kernel/drivers/video/virtio/scripts/presentation.py')
oracle=importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)

# ====== ScreenshotTests ======

class ScreenshotTests(unittest.TestCase):
    def test_scenes_and_rejections(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'kernel/build',prefix='scanout-oracle-') as folder:
            path=Path(folder)/'frame.ppm'
            width,height=128,96
            first=bytearray()
            for y in range(height):
                for x in range(width): first+=bytes((x//32,y//16,(x^y)&255))
            header=f'P6\n{width} {height}\n255\n'.encode()
            path.write_bytes(header+first)
            self.assertEqual(oracle.pixels(path,width,height,1),width*height)
            second=bytearray(first)
            for y in range(24,56):
                for x in range(16,80):
                    at=(y*width+x)*3
                    second[at:at+3]=bytes((224,64,160))
            path.write_bytes(header+second)
            self.assertEqual(oracle.pixels(path,width,height,2),width*height)
            for raw in (header+second[:-1],b'P6\n1 1\n255\n'+second,header+first):
                path.write_bytes(raw)
                with self.assertRaises(RuntimeError): oracle.pixels(path,width,height,2)
            second[-1]^=1
            path.write_bytes(header+second)
            with self.assertRaises(RuntimeError): oracle.pixels(path,width,height,2)

if __name__=='__main__': unittest.main()
