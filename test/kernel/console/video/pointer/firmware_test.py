import importlib.util
from pathlib import Path
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[5]
spec = importlib.util.spec_from_file_location('pointer_firmware',ROOT/'kernel/console/video/scripts/pointer/firmware.py')
pointer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pointer)

# ============ pointer firmware failure boundaries ============

class PointerTests(unittest.TestCase):
    def test_position_and_clamp(self):
        self.assertEqual(pointer.expected_position(1024,768,[]),(499,369,0))
        self.assertEqual(pointer.expected_position(1024,768,[(127,-127,7)]*8),(999,738,4))
        self.assertEqual(pointer.expected_position(1024,768,[(-127,127,0)]*8),(0,0,4))
        for width,height in [(0,768),(1024,16),(1921,768),(1024,1081)]:
            with self.assertRaises(RuntimeError): pointer.expected_position(width,height,[])
        with self.assertRaises(RuntimeError): pointer.expected_position(1024,768,[(256,0,0)])

    def test_bitmap_transparency(self):
        counts = [0,0,0]
        for y in range(18):
            for x in range(18):
                pixel = pointer.sample(x,y)
                self.assertLessEqual((pixel & 31) + (pixel >> 5),16)
                counts[0 if pixel == 0 else 2 if pixel == 512 else 1] += 1
        self.assertTrue(all(counts))
        self.assertEqual(sum(counts),324)
        self.assertGreater(pointer.sample(2,0),0)
        self.assertEqual(pointer.sample(1,6),512)
        for x,y in [(-1,0),(18,0),(0,-1),(0,18),(2**32,0)]:
            self.assertEqual(pointer.sample(x,y),0)

    def test_geometry_preserves_row_padding(self):
        fields = dict(WIDTH=800,HEIGHT=600,STRIDE=2048,BASE=0xc0000000,FORMAT=1,USED=2048*600*4,CAPACITY=2048*600*4)
        log = lambda: '\n'.join(f'KERNEL_FB_{field}: 0x{value:016x}' for field,value in fields.items())
        self.assertEqual(pointer.geometry(log())[2],2048)
        for field,value in [('WIDTH',0xffffffff),('STRIDE',799),('USED',1),('CAPACITY',1),('FORMAT',2)]:
            original = fields[field]; fields[field] = value
            with self.assertRaises(RuntimeError): pointer.geometry(log())
            fields[field] = original

    def test_cleanup_must_match(self):
        log = '\n'.join(f'{field}: 0x{value:016x}' for field,value in [
            ('KERNEL_FB_WIDTH',1024),('KERNEL_FB_HEIGHT',768),
            ('KERNEL_POINTER_X',499),('KERNEL_POINTER_Y',369),('KERNEL_POINTER_MOVES',0)])
        with self.assertRaises(RuntimeError): pointer.finish(log,[])
        self.assertEqual(pointer.finish(log+'\nKERNEL_POINTER_CLEAN',[])['X'],499)
        with self.assertRaises(RuntimeError): pointer.finish(log+'\nKERNEL_POINTER_CLEAN',[(1,0,0)])

    def test_repeated_physical_reads_are_fresh(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'kernel/build',prefix='pointer-hmp-') as name:
            directory = Path(name)
            self.assertTrue(directory.resolve().is_relative_to((ROOT/'kernel/build').resolve()))
            monitor = directory/'monitor.log'
            monitor.write_text('00001000: 0xffffffffffffffff\n',encoding='utf-8')
            class VM:
                value = 1
                def __init__(self): self.stdin = self
                def write(self,text):
                    address = int(text.rsplit('0x',1)[1],16)
                    with monitor.open('a',encoding='utf-8') as stream:
                        for index in range(9): stream.write(f'{address+index*8:08x}: 0x{self.value:016x}\n')
                def flush(self): pass
                def poll(self): return None
            vm = VM()
            self.assertEqual(pointer.physical(vm,monitor,4096),[1]*9)
            vm.value = 2
            self.assertEqual(pointer.physical(vm,monitor,4096),[2]*9)
            import sys
            sys.path.insert(0,str(ROOT/'arch/x86/kernel/drivers/video/framebuffer/scripts'))
            framebuffer = pointer.load('fresh_framebuffer','arch/x86/kernel/drivers/video/framebuffer/scripts/firmware.py')
            vm.value = 3
            self.assertEqual(framebuffer.physical(vm,monitor,4096,4),[3]*4)
            vm.value = 4
            self.assertEqual(framebuffer.physical(vm,monitor,4096,4),[4]*4)

    def test_fallback_still_requires_hardware_input_and_cleanup(self):
        mouse = pointer.load('mouse_evidence','kernel/drivers/input/ps2/scripts/mouse/firmware.py')
        expected = [(1, -1, 0)]
        fields = dict(MOUSE_BYTES=3,MOUSE_EVENTS=1,MOUSE_IRQS=1,MOUSE_LOSSES=0,MOUSE_TASK_IRQS=0)
        def log():
            return ('MOUSE_EVENT: 0x0000000000000001 -0x0000000000000001 0x0000000000000000\n'
                    'KERNEL_PS2_MOUSE_CLEANUP_OK\n' + '\n'.join(f'{key}: 0x{value:016x}' for key,value in fields.items()))
        trace = 'Servicing hardware INT=0x42'
        with self.assertRaises(RuntimeError): mouse.verify(log(),trace,expected,pointer.number)
        self.assertEqual(mouse.verify(log(),trace,expected,pointer.number,False)['MOUSE_TASK_IRQS'],0)
        for field,value in [('MOUSE_BYTES',2),('MOUSE_EVENTS',0),('MOUSE_IRQS',0),('MOUSE_LOSSES',1)]:
            saved = fields[field]; fields[field] = value
            with self.assertRaises(RuntimeError): mouse.verify(log(),trace,expected,pointer.number,False)
            fields[field] = saved
        with self.assertRaises(RuntimeError): mouse.verify(log(),'no IRQ',expected,pointer.number,False)
        with self.assertRaises(RuntimeError): mouse.verify(log().replace('KERNEL_PS2_MOUSE_CLEANUP_OK',''),trace,expected,pointer.number,False)
        with self.assertRaises(RuntimeError): mouse.verify(log(),trace,[(2,-1,0)],pointer.number,False)

if __name__ == '__main__':
    unittest.main()
