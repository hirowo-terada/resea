use crate::syscalls;
use crate::message::{Msg, Header};

#[derive(Copy, Clone, Debug)]
#[repr(transparent)]
pub struct CId(isize);

impl CId {
    pub fn new(cid: isize) -> CId {
        CId(cid)
    }

    pub fn as_isize(&self) -> isize {
        self.0
    }
}

#[derive(Debug)]
#[repr(transparent)]
pub struct Channel {
    cid: CId,
}

impl Channel {
    pub fn from_cid(cid: CId) -> Channel {
        Channel {
            cid
        }
    }

    pub fn create() -> syscalls::Result<(Channel)> {
        syscalls::open().map(Channel::from_cid)
    }

    pub fn cid(&self) -> CId {
        self.cid
    }

    pub fn link(&self, ch: &Channel) -> syscalls::Result<()> {
        syscalls::link(self.cid, ch.cid)
    }

    pub fn transfer_to(&self, dest: &Channel) -> syscalls::Result<()> {
        syscalls::transfer(self.cid, dest.cid)
    }

    pub fn send<T: Msg>(&self, m: T) -> syscalls::Result<()> {
        unsafe {
            syscalls::update_thread_local_buffer(&m);
            syscalls::send(self.cid)
        }
    }

    pub fn peek_header(&self) -> Header {
        unsafe {
            syscalls::read_thread_local_buffer()
        }
    }

    pub fn read_buffer<T: Msg>(&self) -> T {
        unsafe {
            syscalls::read_thread_local_buffer()
        }
    }

    pub fn wait(&self) -> syscalls::Result<()> {
        syscalls::recv(self.cid)
    }

    pub fn recv<T: Msg>(&self) -> syscalls::Result<T> {
        unsafe {
            syscalls::recv(self.cid)
                .map(|_| syscalls::read_thread_local_buffer())
        }
    }

    pub fn call<T: Msg, U: Msg>(&self, m: T) -> syscalls::Result<U> {
        unsafe {
            syscalls::update_thread_local_buffer(&m);
            syscalls::call(self.cid)
                .map(|_| syscalls::read_thread_local_buffer())
        }
    }
}
