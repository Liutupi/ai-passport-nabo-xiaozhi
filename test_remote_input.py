"""Compile production gesture handling: single/hold/double-click separation."""
import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest
from host_tools import CC, SHARED_EXPORT, unload_library

ROOT=Path(__file__).resolve().parent
BOARD=ROOT/'xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo'
DOWN,OK,UP,DOUBLE=0,1,2,3
SCROLL_UP,SCROLL_DOWN,RIGHT,LEFT,NONE=0,1,25,26,27

class Input(ctypes.Structure):
    _fields_=[('held',ctypes.c_int),('since',ctypes.c_uint32),('next',ctypes.c_uint32),
              ('pending',ctypes.c_bool),('repeated',ctypes.c_bool)]

class Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.temp.cleanup)
        dll=Path(cls.temp.name)/'remote.dll'
        subprocess.run([CC,'-std=c11','-shared',SHARED_EXPORT,'-Wall','-Wextra','-Werror',
                        '-I',str(BOARD),str(BOARD/'nabo_remote_input.c'),'-o',str(dll)],check=True)
        cls.lib=ctypes.CDLL(str(dll));cls.addClassCleanup(unload_library,cls.lib._handle)
        cls.lib.nabo_remote_reset.argtypes=[ctypes.POINTER(Input)]
        cls.lib.nabo_remote_press.argtypes=[ctypes.POINTER(Input),ctypes.c_int,ctypes.c_bool,ctypes.c_uint32]
        cls.lib.nabo_remote_click.argtypes=[ctypes.POINTER(Input),ctypes.c_int]
        cls.lib.nabo_remote_tick.argtypes=[ctypes.POINTER(Input),ctypes.c_uint32]
    def setUp(self):
        self.state=Input();self.lib.nabo_remote_reset(ctypes.byref(self.state))
    def press(self,key,down,time):return self.lib.nabo_remote_press(ctypes.byref(self.state),key,down,time)
    def click(self,key):return self.lib.nabo_remote_click(ctypes.byref(self.state),key)
    def tick(self,time):return self.lib.nabo_remote_tick(ctypes.byref(self.state),time)
    def test_single_up_once_after_click_classification(self):
        self.assertEqual(self.press(UP,True,0),NONE);self.assertEqual(self.press(UP,False,40),NONE)
        self.assertEqual(self.click(UP),SCROLL_UP);self.assertEqual(self.click(UP),NONE)
    def test_double_up_navigates_once_without_scroll(self):
        actions=[self.press(UP,True,0),self.press(UP,False,30),self.press(UP,True,90),self.press(UP,False,120),self.click(DOUBLE)]
        self.assertEqual(actions,[NONE,NONE,NONE,NONE,LEFT]);self.assertEqual(self.click(UP),NONE)
        self.assertEqual(self.tick(1000),NONE)
    def test_held_up_repeats_and_release_adds_no_click(self):
        self.press(UP,True,0)
        self.assertEqual(self.tick(449),NONE);self.assertEqual(self.tick(450),SCROLL_UP)
        self.assertEqual(self.tick(609),NONE);self.assertEqual(self.tick(610),SCROLL_UP)
        self.press(UP,False,620);self.assertEqual(self.click(UP),NONE);self.assertEqual(self.tick(900),NONE)
    def test_down_is_immediate_and_repeats(self):
        self.assertEqual(self.press(DOWN,True,0),SCROLL_DOWN)
        self.assertEqual(self.tick(449),NONE);self.assertEqual(self.tick(450),SCROLL_DOWN)
        self.press(DOWN,False,480);self.assertEqual(self.click(DOWN),NONE)
    def test_right_cancels_pending_up_and_repeat(self):
        self.press(UP,True,0);self.assertEqual(self.click(OK),RIGHT)
        self.assertEqual(self.tick(700),NONE);self.assertEqual(self.click(UP),NONE)
    def test_left_cancels_down_repeat(self):
        self.press(DOWN,True,0);self.assertEqual(self.click(DOUBLE),LEFT)
        self.assertEqual(self.tick(700),NONE)
    def test_reset_cancels_every_action(self):
        self.press(UP,True,0);self.lib.nabo_remote_reset(ctypes.byref(self.state))
        self.assertEqual(self.tick(1000),NONE);self.assertEqual(self.click(UP),NONE)
    def test_missing_release_times_out(self):
        self.press(UP,True,0);self.assertEqual(self.tick(15001),NONE)
        self.assertEqual(self.state.held,-1);self.assertEqual(self.click(UP),NONE)
    def test_repeat_clock_wrap(self):
        start=0xffffff00;self.press(UP,True,start)
        self.assertEqual(self.tick((start+449)&0xffffffff),NONE)
        self.assertEqual(self.tick((start+450)&0xffffffff),SCROLL_UP)
    def test_new_down_cancels_pending_up(self):
        self.press(UP,True,0);self.press(UP,False,20)
        self.assertEqual(self.press(DOWN,True,50),SCROLL_DOWN)
        self.assertEqual(self.click(UP),NONE);self.assertEqual(self.tick(500),SCROLL_DOWN)
    def test_unrelated_release_does_not_cancel_hold(self):
        self.press(UP,True,0);self.press(DOWN,False,30);self.assertEqual(self.tick(450),SCROLL_UP)
    def test_two_separate_single_clicks_both_scroll(self):
        for time in [0,1000]:
            self.press(UP,True,time);self.press(UP,False,time+30)
            self.assertEqual(self.click(UP),SCROLL_UP)

if __name__=='__main__':unittest.main()
