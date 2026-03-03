import mgba.gba as gba


def test_rfu_lifecycle():
    rfu = gba.RFUDriver()
    assert not rfu.is_connected()
    assert rfu.connect()
    assert rfu.is_connected()
    rfu.disconnect()
    assert not rfu.is_connected()
