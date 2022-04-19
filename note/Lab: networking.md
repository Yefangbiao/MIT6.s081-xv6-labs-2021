# Lab: networking

按照Hints的思路来。其实网卡是一种约定的规定，按照规定编码。

`e1000_transmit`

```c
int
e1000_transmit(struct mbuf *m)
{
  //
  // Your code here.
  //
  // the mbuf contains an ethernet frame; program it into
  // the TX descriptor ring so that the e1000 sends it. Stash
  // a pointer so that it can be freed after sending.
  //
    acquire(&e1000_lock);
    // First ask the E1000 for the TX ring index at which it's expecting the next packet,
    // by reading the E1000_TDT control register.
    uint32 position = regs[E1000_TDT];
    // Then check if the the ring is overflowing.
    // If E1000_TXD_STAT_DD is not set in the descriptor indexed by E1000_TDT,
    // the E1000 hasn't finished the corresponding previous transmission request, so return an error.
    struct tx_desc * desc = &tx_ring[position];
    if ((desc->status & E1000_TXD_STAT_DD) == 0) {
        release(&e1000_lock);
        return -1;
    }
    // Otherwise, use mbuffree() to free the last mbuf that was transmitted from that descriptor (if there was one).
    if (tx_mbufs[position] != 0)
        mbuffree(tx_mbufs[position]);

    // Then fill in the descriptor. m->head points to the packet's content in memory,
    // and m->len is the packet length.
    // Set the necessary cmd flags (look at Section 3.3 in the E1000 manual)
    // and stash away a pointer to the mbuf for later freeing.
    desc->addr = (uint64)m->head;
    desc->length = m->len;
    desc->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS;
    tx_mbufs[position] = m;

    // Finally, update the ring position by adding one to E1000_TDT modulo TX_RING_SIZE.
    regs[E1000_TDT] = (position + 1) % TX_RING_SIZE;

    // If e1000_transmit() added the mbuf successfully to the ring, return 0.
    // On failure (e.g., there is no descriptor available to transmit the mbuf),
    // return -1 so that the caller knows to free the mbuf.
    __sync_synchronize();
    release(&e1000_lock);

  return 0;
}
```



`e1000_recv`

```c
static void
e1000_recv(void)
{
  //
  // Your code here.
  //
  // Check for packets that have arrived from the e1000
  // Create and deliver an mbuf for each packet (using net_rx()).
  //

  uint32 rdt_position;
  struct rx_desc* desc;

  while (1) {
      acquire(&e1000_lock);
      // First ask the E1000 for the ring index at which the next waiting received packet (if any) is located,
      // by fetching the E1000_RDT control register and adding one modulo RX_RING_SIZE.
      rdt_position = (regs[E1000_RDT] + 1) % RX_RING_SIZE;
      desc = &rx_ring[rdt_position];

      // Then check if a new packet is available by checking for the E1000_RXD_STAT_DD bit in the status portion of the descriptor.
      // If not, stop.
      if ((desc->status & E1000_TXD_STAT_DD) == 0) {
          release(&e1000_lock);
          return;
      }

      // Otherwise, update the mbuf's m->len to the length reported in the descriptor.
      // Deliver the mbuf to the network stack using net_rx().
      struct mbuf* buf = rx_mbufs[rdt_position];
      mbufput(buf, desc->length);

      // Then allocate a new mbuf using mbufalloc() to replace the one just given to net_rx().
      // Program its data pointer (m->head) into the descriptor. Clear the descriptor's status bits to zero.
      struct mbuf *newmbuf = mbufalloc(0);
      rx_mbufs[rdt_position] = newmbuf;
      desc->addr = (uint64) rx_mbufs[rdt_position]->head;
      desc->status = 0;

      // Finally, update the E1000_RDT register to be the index of the last ring descriptor processed.
      regs[E1000_RDT] = rdt_position;

      __sync_synchronize();
      release(&e1000_lock);

      // using net_rx().
      // 释放锁后调用
      net_rx(buf);

  }

}
```


