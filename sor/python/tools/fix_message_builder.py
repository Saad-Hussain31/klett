"""FIX protocol message builder for testing.

Generates FIX 4.2-style messages for New Order Single, Cancel Request,
Execution Reports, and other common message types. Useful for testing
FIX gateway parsing and connectivity.
"""

import time
from typing import Optional


class FixMessageBuilder:
    """Builds FIX 4.2 protocol messages as strings.

    Produces tag=value|SOH formatted messages suitable for feeding into
    FIX parsers or gateway tests.
    """

    SOH = "\x01"  # Standard FIX delimiter
    FIX_VERSION = "FIX.4.2"

    def __init__(self, sender_comp_id: str = "SOR_CLIENT",
                 target_comp_id: str = "EXCHANGE"):
        self.sender_comp_id = sender_comp_id
        self.target_comp_id = target_comp_id
        self._seq_num = 1

    def new_order_single(
        self,
        cl_ord_id: str,
        symbol: str,
        side: str,
        quantity: float,
        price: Optional[float] = None,
        order_type: str = "limit",
        time_in_force: str = "day",
        account: str = "ACCT001",
    ) -> str:
        """Build a NewOrderSingle (MsgType=D) message.

        Args:
            cl_ord_id: Client order ID (tag 11).
            symbol: Instrument symbol (tag 55).
            side: 'buy' or 'sell' (tag 54: 1=Buy, 2=Sell).
            quantity: Order quantity (tag 38).
            price: Limit price (tag 44), None for market orders.
            order_type: 'limit', 'market', 'ioc', 'fok'.
            time_in_force: 'day', 'gtc', 'ioc', 'fok'.
            account: Account identifier (tag 1).
        """
        side_val = "1" if side.lower() == "buy" else "2"
        ord_type_map = {"limit": "2", "market": "1", "ioc": "2", "fok": "2"}
        tif_map = {"day": "0", "gtc": "1", "ioc": "3", "fok": "4"}

        fields = {
            35: "D",           # MsgType
            11: cl_ord_id,     # ClOrdID
            1: account,        # Account
            55: symbol,        # Symbol
            54: side_val,      # Side
            38: str(int(quantity)),  # OrderQty
            40: ord_type_map.get(order_type.lower(), "2"),  # OrdType
            59: tif_map.get(time_in_force.lower(), "0"),    # TimeInForce
            60: self._transact_time(),  # TransactTime
        }
        if price is not None:
            fields[44] = f"{price:.4f}"  # Price

        return self._build_message(fields)

    def cancel_request(
        self,
        cl_ord_id: str,
        orig_cl_ord_id: str,
        symbol: str,
        side: str,
        quantity: float,
    ) -> str:
        """Build an OrderCancelRequest (MsgType=F) message."""
        side_val = "1" if side.lower() == "buy" else "2"
        fields = {
            35: "F",
            11: cl_ord_id,
            41: orig_cl_ord_id,
            55: symbol,
            54: side_val,
            38: str(int(quantity)),
            60: self._transact_time(),
        }
        return self._build_message(fields)

    def execution_report(
        self,
        cl_ord_id: str,
        exec_id: str,
        symbol: str,
        side: str,
        order_qty: float,
        exec_type: str = "fill",
        ord_status: str = "filled",
        last_px: float = 0.0,
        last_qty: float = 0.0,
        cum_qty: float = 0.0,
        leaves_qty: float = 0.0,
        avg_px: float = 0.0,
    ) -> str:
        """Build an ExecutionReport (MsgType=8) message."""
        side_val = "1" if side.lower() == "buy" else "2"
        exec_type_map = {
            "new": "0", "partial_fill": "1", "fill": "2",
            "canceled": "4", "rejected": "8",
        }
        ord_status_map = {
            "new": "0", "partially_filled": "1", "filled": "2",
            "canceled": "4", "rejected": "8",
        }
        fields = {
            35: "8",
            11: cl_ord_id,
            17: exec_id,
            55: symbol,
            54: side_val,
            38: str(int(order_qty)),
            150: exec_type_map.get(exec_type.lower(), "0"),
            39: ord_status_map.get(ord_status.lower(), "0"),
            31: f"{last_px:.4f}",
            32: str(int(last_qty)),
            14: str(int(cum_qty)),
            151: str(int(leaves_qty)),
            6: f"{avg_px:.4f}",
            60: self._transact_time(),
        }
        return self._build_message(fields)

    def heartbeat(self, test_req_id: Optional[str] = None) -> str:
        """Build a Heartbeat (MsgType=0) message."""
        fields = {35: "0"}
        if test_req_id:
            fields[112] = test_req_id
        return self._build_message(fields)

    def logon(self, encrypt_method: int = 0, heartbeat_int: int = 30) -> str:
        """Build a Logon (MsgType=A) message."""
        fields = {
            35: "A",
            98: str(encrypt_method),
            108: str(heartbeat_int),
        }
        return self._build_message(fields)

    def logout(self, text: str = "") -> str:
        """Build a Logout (MsgType=5) message."""
        fields = {35: "5"}
        if text:
            fields[58] = text
        return self._build_message(fields)

    def parse_message(self, raw: str) -> dict:
        """Parse a FIX message string into a dict of tag -> value."""
        result = {}
        pairs = raw.split(self.SOH)
        for pair in pairs:
            if "=" in pair:
                tag, value = pair.split("=", 1)
                result[int(tag)] = value
        return result

    # ------------------------------------------------------------------
    # Internal
    # ------------------------------------------------------------------

    def _build_message(self, fields: dict) -> str:
        """Assemble a complete FIX message with header and checksum."""
        # Body (excluding BeginString, BodyLength, CheckSum)
        body_fields = {
            49: self.sender_comp_id,
            56: self.target_comp_id,
            34: str(self._seq_num),
            52: self._sending_time(),
        }
        body_fields.update(fields)
        self._seq_num += 1

        body = self.SOH.join(f"{k}={v}" for k, v in body_fields.items()) + self.SOH

        # Header
        header = f"8={self.FIX_VERSION}{self.SOH}9={len(body)}{self.SOH}"

        # Checksum
        raw = header + body
        checksum = sum(ord(c) for c in raw) % 256
        full = raw + f"10={checksum:03d}{self.SOH}"

        return full

    @staticmethod
    def _transact_time() -> str:
        return time.strftime("%Y%m%d-%H:%M:%S.000")

    @staticmethod
    def _sending_time() -> str:
        return time.strftime("%Y%m%d-%H:%M:%S")
