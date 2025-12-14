import chisel3._
import circt.stage.ChiselStage
import chisel3.util._

// filter -> RangeReduce -> poly -> compose
object SFUOp {
  val EXP2  = 0.U(3.W)
  val LOG2  = 1.U(3.W)
  val RCP   = 2.U(3.W)
  val SQRT  = 3.U(3.W)
  val RSQRT = 4.U(3.W)
}

// just for readability, do not change values
object SFUConfig {
  val c0Width            = 26
  val c1Width            = 16
  val c2Width            = 12
  val squarerOutputWidth = 15
}

// constants for special values, changes may crash the hardware behavior
case class FunctionParams(
  m:      Int,
  c0Sign: Int,
  c1Sign: Int,
  c2Sign: Int,
  c0Exp:  Int,
  c1Exp:  Int,
  c2Exp:  Int
) {
  val c0RealExp:    Int = c0Exp - SFUConfig.c0Width + 1
  val c1RealExp:    Int = c1Exp - SFUConfig.c1Width + 1
  val c2RealExp:    Int = c2Exp - SFUConfig.c2Width + 1
  val c1XlRealExp:  Int = c1RealExp - 23
  val c2Xl2RealExp: Int = c2RealExp - 2 * m - SFUConfig.squarerOutputWidth
  val shift0:       Int = 2 * (23 - m) - SFUConfig.squarerOutputWidth
  val shift1:       Int = c0RealExp - c1XlRealExp
  val shift2:       Int = c0RealExp - c2Xl2RealExp
}

object Function {
  val EXP2  = FunctionParams(6, 1,  1,  1, 1,  1, -1)
  val LOG2  = FunctionParams(6, 1,  1, -1, 0,  1,  0)
  val RCP   = FunctionParams(7, 1, -1,  1, 0,  0,  0)
  val SQRT  = FunctionParams(6, 1,  1, -1, 0, -1, -3)
  val RSQRT = FunctionParams(6, 1, -1,  1, 0, -1, -1)

  def getShift0(op: UInt): UInt = {
    MuxLookup(op, 0.U(5.W)) (Seq(
      SFUOp.EXP2  -> EXP2.shift0.U(5.W),
      SFUOp.LOG2  -> LOG2.shift0.U(5.W),
      SFUOp.RCP   -> RCP.shift0.U(5.W),
      SFUOp.SQRT  -> SQRT.shift0.U(5.W),
      SFUOp.RSQRT -> RSQRT.shift0.U(5.W)
    ))
  }

  def getShift1(op: UInt): UInt = {
    MuxLookup(op, 0.U(5.W)) (Seq(
      SFUOp.EXP2  -> EXP2.shift1.U(5.W),
      SFUOp.LOG2  -> LOG2.shift1.U(5.W),
      SFUOp.RCP   -> RCP.shift1.U(5.W),
      SFUOp.SQRT  -> SQRT.shift1.U(5.W),
      SFUOp.RSQRT -> RSQRT.shift1.U(5.W)
    ))
  }
  def getShift2(op: UInt): UInt = {
    MuxLookup(op, 0.U(5.W)) (Seq(
      SFUOp.EXP2  -> EXP2.shift2.U(5.W),
      SFUOp.LOG2  -> LOG2.shift2.U(5.W),
      SFUOp.RCP   -> RCP.shift2.U(5.W),
      SFUOp.SQRT  -> SQRT.shift2.U(5.W),
      SFUOp.RSQRT -> RSQRT.shift2.U(5.W)
    ))
  }

}

object SFUParameters {
  val MIN_INPUT_EXP2 = "hC3000000".U(32.W) // -128.0
  val MAX_INPUT_EXP2 = "h43000000".U(32.W) // 128.0

  val POS_ZERO       = "h00000000".U(32.W)
  val POS_ONE        = "h3F800000".U(32.W)
  val POS_INF        = "h7F800000".U(32.W)

  val NEG_ZERO       = "h80000000".U(32.W)
  val NEG_ONE        = "hBF800000".U(32.W)
  val NEG_INF        = "hFF800000".U(32.W)

  val NAN            = "h7FFFFFFF".U(32.W)
}

object SFUUtils {
  implicit class DecoupledPipe[T <: Data](val decoupledBundle: DecoupledIO[T]) extends AnyVal {
    def handshakePipeIf(en: Boolean): DecoupledIO[T] = {
      if (en) {
        val out    = Wire(Decoupled(chiselTypeOf(decoupledBundle.bits)))
        val rValid = RegInit(false.B)
        val rBits  = Reg(chiselTypeOf(decoupledBundle.bits))
        decoupledBundle.ready  := !rValid || out.ready
        out.valid              := rValid
        out.bits               := rBits
        when(decoupledBundle.fire) {
          rBits  := decoupledBundle.bits
          rValid := true.B
        } .elsewhen(out.fire) {
          rValid := false.B
        }
        out
      } else {
        decoupledBundle
      }
    }
  }
}

import SFUUtils._


class Filter[T <: Bundle](throughoutGen: => T) extends Module {
  class InBundle extends Bundle {
    val x          = UInt(32.W)
    val op         = UInt(3.W)
    val throughout = throughoutGen.cloneType
  }
  class OutBundle extends Bundle {
    val sign       = UInt(1.W)
    val exponent   = UInt(8.W)
    val mantissa   = UInt(23.W)
    val bypass     = Bool()
    val bypassVal  = UInt(32.W)
    val throughout = throughoutGen.cloneType
  }
  val io = IO(new Bundle {
    val in  = Flipped(Decoupled(new InBundle))
    val out = Decoupled(new OutBundle)
  })
 
  val s = io.in.bits.x(31)
  val e = io.in.bits.x(30, 23)
  val m = io.in.bits.x(22, 0)
 
  val isZero   =  e === 0.U // subnormal numbers are treated as zero
  val isInf    = (e === "hFF".U) && (m === 0.U)
  val isNaN    = (e === "hFF".U) && (m =/= 0.U)
  val isNeg    =  s === 1.U
 
  val tooBig = (!s) && (io.in.bits.x > SFUParameters.MAX_INPUT_EXP2)
  val tooNeg =   s  && (io.in.bits.x > SFUParameters.MIN_INPUT_EXP2)

  val bypass    = Wire(Bool())
  val bypassVal = Wire(UInt(32.W))

  when (io.in.bits.op === SFUOp.EXP2) {
    bypass    := isZero || isInf || isNaN || tooBig || tooNeg
    bypassVal := MuxCase(SFUParameters.POS_ZERO, Seq(
      isZero  -> SFUParameters.POS_ONE,
      isInf   -> Mux(isNeg, SFUParameters.POS_ZERO, SFUParameters.POS_INF),
      isNaN   -> SFUParameters.NAN,
      tooBig  -> SFUParameters.POS_INF,
      tooNeg  -> SFUParameters.POS_ZERO
    ))
  } .elsewhen (io.in.bits.op === SFUOp.LOG2) {
    bypass    := isNeg || isZero || isInf || isNaN
    bypassVal := MuxCase(SFUParameters.POS_ZERO, Seq(
      isNeg  -> SFUParameters.NAN,
      isZero -> SFUParameters.NEG_INF,
      isInf  -> SFUParameters.POS_INF,
      isNaN  -> SFUParameters.NAN
    ))
  } .elsewhen (io.in.bits.op === SFUOp.RCP) {
    bypass    := isZero || isNaN || isInf
    bypassVal := MuxCase(SFUParameters.POS_ZERO, Seq(
      isZero -> Mux(isNeg, SFUParameters.NEG_INF, SFUParameters.POS_INF),
      isNaN  -> SFUParameters.NAN,
      isInf  -> Mux(isNeg, SFUParameters.NEG_ZERO, SFUParameters.POS_ZERO)
    ))
  } .elsewhen (io.in.bits.op === SFUOp.SQRT) {
    bypass    := isNeg || isZero || isInf || isNaN
    bypassVal := MuxCase(SFUParameters.POS_ZERO, Seq(
      isNeg  -> SFUParameters.NAN,
      isZero -> SFUParameters.POS_ZERO,
      isInf  -> SFUParameters.POS_INF,
      isNaN  -> SFUParameters.NAN
    ))
  } .elsewhen (io.in.bits.op === SFUOp.RSQRT) {
    bypass    := isNeg || isZero || isInf || isNaN
    bypassVal := MuxCase(SFUParameters.POS_ZERO, Seq(
      isNeg  -> SFUParameters.NAN,
      isZero -> SFUParameters.POS_INF,
      isInf  -> SFUParameters.POS_ZERO,
      isNaN  -> SFUParameters.NAN
    ))
  } .otherwise {
    bypass    := false.B
    bypassVal := SFUParameters.POS_ZERO
  }
 
  val s1 = Wire(Decoupled(new OutBundle))
  val s1Pipe = s1.handshakePipeIf(true)
 
  s1.valid           := io.in.valid
  s1.bits.sign       := s
  s1.bits.exponent   := e
  s1.bits.mantissa   := m
  s1.bits.bypass     := bypass
  s1.bits.bypassVal  := bypassVal
  s1.bits.throughout := io.in.bits.throughout
  io.in.ready        := s1.ready
 
  io.out <> s1Pipe
}

class RangeReduce[T <: Bundle](throughoutGen: => T) extends Module {
  class InBundle extends Bundle {
    val sign       = UInt(1.W)
    val exponent   = UInt(8.W)
    val mantissa   = UInt(23.W)
    val op         = UInt(3.W)
    val throughout = throughoutGen.cloneType
  }
  class OutBundle extends Bundle {
    val index      = UInt(7.W)
    val xl         = UInt(17.W)
    val exp        = SInt(8.W)
    val throughout = throughoutGen.cloneType
  }
 
  val io = IO(new Bundle {
    val in  = Flipped(Decoupled(new InBundle))
    val out = Decoupled(new OutBundle)
  })

  val op       = io.in.bits.op
  val sign     = io.in.bits.sign.asBool
  val rawExp   = io.in.bits.exponent
  val mantissa = io.in.bits.mantissa
  val sig      = Cat(1.U(1.W), mantissa)

  // exp2 specific range reduction
  val expSigned     = (rawExp.zext - 127.S).asSInt
  val sigExtended   = Cat(0.U(7.W), sig)
  val shift         = expSigned
  val sigShifted    = Mux(shift >= 0.S, sigExtended << shift.asUInt, sigExtended >> (-shift).asUInt)
  val intPart       = sigShifted(30, 23)
  val fracPart      = sigShifted(22, 0)
  val isFracZero    = fracPart === 0.U
  val intPartFloor  = Mux(sign && (!isFracZero), intPart + 1.U, intPart)
  val fracPartFloor = Mux(sign && (!isFracZero), (1.U << 23) - fracPart, fracPart)

  val exp = MuxLookup(op, 0.S(8.W)) (Seq(
    SFUOp.EXP2  -> Mux(sign, -(intPartFloor.asSInt), intPartFloor.asSInt),
    SFUOp.LOG2  -> expSigned,
    SFUOp.RCP   -> expSigned,
    SFUOp.SQRT  -> expSigned,
    SFUOp.RSQRT -> expSigned
  ))

  val index = MuxLookup(op, 0.U(7.W)) (Seq(
    SFUOp.EXP2  -> Cat(0.U(1.W), fracPartFloor(22, 17)),
    SFUOp.LOG2  -> Cat(0.U(1.W), mantissa(22, 17)),
    SFUOp.RCP   -> mantissa(22, 16),
    SFUOp.SQRT  -> Cat(expSigned(0), mantissa(22, 17)),
    SFUOp.RSQRT -> Cat(expSigned(0), mantissa(22, 17))
  ))

  val xl = MuxLookup(op, 0.U(32.W)) (Seq(
    SFUOp.EXP2  -> fracPartFloor(16, 0),
    SFUOp.LOG2  -> mantissa(16, 0),
    SFUOp.RCP   -> Cat(0.U(1.W), mantissa(15, 0)),
    SFUOp.SQRT  -> mantissa(16, 0),
    SFUOp.RSQRT -> mantissa(16, 0)
  ))
 
  val s1     = Wire(Decoupled(new OutBundle))
  val s1Pipe = s1.handshakePipeIf(true)
  io.in.ready        := s1.ready
  s1.valid           := io.in.valid
  s1.bits.exp        := exp
  s1.bits.index      := index
  s1.bits.xl         := xl
  s1.bits.throughout := io.in.bits.throughout
 
  io.out <> s1Pipe
}

class LookupTable[T <: Bundle](throughoutGen: => T) extends Module {
  class InBundle extends Bundle {
    val op         = UInt(3.W)
    val index      = UInt(7.W)
    val throughout = throughoutGen.cloneType
  }
 
  class OutBundle extends Bundle {
    val c0         = SInt(27.W)
    val c1         = SInt(17.W)
    val c2         = SInt(13.W)
    val throughout = throughoutGen.cloneType
  }
 
  val io = IO(new Bundle {
    val in  = Flipped(Decoupled(new InBundle))
    val out = Decoupled(new OutBundle)
  })
 
  class LUTEntry extends Bundle {
    val c0 = SInt(27.W)
    val c1 = SInt(17.W)
    val c2 = SInt(13.W)
  }
 
  def loadLUT(filename: String, c0Sign: Int, c1Sign: Int, c2Sign: Int): Seq[LUTEntry] = {
    val lines = scala.io.Source.fromFile(filename).getLines().toSeq
    lines.map { line =>
      val parts = line.trim.split("\\s+")
      val c0Hex = parts(0)
      val c1Hex = parts(1)
      val c2Hex = parts(2)

      val c0Unsigned = BigInt(c0Hex, 16)
      val c1Unsigned = BigInt(c1Hex, 16)
      val c2Unsigned = BigInt(c2Hex, 16)

      val c0Signed = if (c0Sign == -1) -c0Unsigned else c0Unsigned
      val c1Signed = if (c1Sign == -1) -c1Unsigned else c1Unsigned
      val c2Signed = if (c2Sign == -1) -c2Unsigned else c2Unsigned
 
      val entry = Wire(new LUTEntry)
      entry.c0 := c0Signed.S(27.W)
      entry.c1 := c1Signed.S(17.W)
      entry.c2 := c2Signed.S(13.W)
      entry
    }
  }
 
  val lutPath = sys.env.getOrElse("LUT_PATH", "lut")

  val exp2LUT      = VecInit(loadLUT(s"$lutPath/exp2-coeffs.txt",       Function.EXP2.c0Sign,  Function.EXP2.c1Sign,  Function.EXP2.c2Sign))
  val log2LUT      = VecInit(loadLUT(s"$lutPath/log2-coeffs.txt",       Function.LOG2.c0Sign,  Function.LOG2.c1Sign,  Function.LOG2.c2Sign))
  val rcpLUT       = VecInit(loadLUT(s"$lutPath/rcp-coeffs.txt",        Function.RCP.c0Sign,   Function.RCP.c1Sign,   Function.RCP.c2Sign))
  val sqrtEvenLUT  = VecInit(loadLUT(s"$lutPath/sqrt-even-coeffs.txt",  Function.SQRT.c0Sign,  Function.SQRT.c1Sign,  Function.SQRT.c2Sign))
  val sqrtOddLUT   = VecInit(loadLUT(s"$lutPath/sqrt-odd-coeffs.txt",   Function.SQRT.c0Sign,  Function.SQRT.c1Sign,  Function.SQRT.c2Sign))
  val rsqrtEvenLUT = VecInit(loadLUT(s"$lutPath/rsqrt-even-coeffs.txt", Function.RSQRT.c0Sign, Function.RSQRT.c1Sign, Function.RSQRT.c2Sign))
  val rsqrtOddLUT  = VecInit(loadLUT(s"$lutPath/rsqrt-odd-coeffs.txt",  Function.RSQRT.c0Sign, Function.RSQRT.c1Sign, Function.RSQRT.c2Sign))

  val op    = io.in.bits.op
  val index = io.in.bits.index
 
  val entry = MuxLookup(op, rcpLUT(index))(Seq(
    SFUOp.EXP2  -> exp2LUT(index(5, 0)),
    SFUOp.LOG2  -> log2LUT(index(5, 0)),
    SFUOp.RCP   -> rcpLUT(index),
    SFUOp.SQRT  -> Mux(index(6), sqrtOddLUT(index(5, 0)), sqrtEvenLUT(index(5, 0))),
    SFUOp.RSQRT -> Mux(index(6), rsqrtOddLUT(index(5, 0)), rsqrtEvenLUT(index(5, 0)))
  ))
 
  val s1 = Wire(Decoupled(new OutBundle))
  val s1Pipe = s1.handshakePipeIf(true)
 
  io.in.ready        := s1.ready
  s1.valid           := io.in.valid
  s1.bits.c0         := entry.c0
  s1.bits.c1         := entry.c1
  s1.bits.c2         := entry.c2
  s1.bits.throughout := io.in.bits.throughout
 
  io.out <> s1Pipe
}

class Poly[T <: Bundle](throughoutGen: => T) extends Module {
  class InBundle extends Bundle {
    val c0         = SInt(27.W)
    val c1         = SInt(17.W)
    val c2         = SInt(13.W)
    val xl         = UInt(17.W)
    val op         = UInt(3.W)
    val throughout = throughoutGen.cloneType
  }
 
  class OutBundle extends Bundle {
    val result     = UInt(26.W)
    val throughout = throughoutGen.cloneType
  }
 
  val io = IO(new Bundle {
    val in  = Flipped(Decoupled(new InBundle))
    val out = Decoupled(new OutBundle)
  })
 
  // Stage 1: Compute and truncate xl^2
  val xl2      = io.in.bits.xl * io.in.bits.xl
  val shift0   = Function.getShift0(io.in.bits.op)
  val aligned0 = (xl2 >> shift0)(14, 0)
 
  val s1 = Wire(Decoupled(new Bundle {
    val op         = UInt(3.W)
    val c0         = SInt(27.W)
    val c1         = SInt(17.W)
    val c2         = SInt(13.W)
    val xl         = UInt(17.W)
    val xl2        = UInt(15.W)
    val throughout = throughoutGen.cloneType
  }))
  val s1Pipe = s1.handshakePipeIf(true)
 
  io.in.ready        := s1.ready
  s1.valid           := io.in.valid
  s1.bits.c0         := io.in.bits.c0
  s1.bits.c1         := io.in.bits.c1
  s1.bits.c2         := io.in.bits.c2
  s1.bits.xl         := io.in.bits.xl
  s1.bits.xl2        := aligned0
  s1.bits.op         := io.in.bits.op
  s1.bits.throughout := io.in.bits.throughout
 
  // Stage 2: Multiply
  val xlSigned  = Cat(0.U(1.W), s1Pipe.bits.xl).asSInt
  val xl2Signed = Cat(0.U(1.W), s1Pipe.bits.xl2).asSInt

  val c2Xl2 = s1Pipe.bits.c2 * xl2Signed
  val c1Xl  = s1Pipe.bits.c1 * xlSigned
 
  val s2 = Wire(Decoupled(new Bundle {
    val op         = UInt(3.W)
    val c0         = SInt(27.W)
    val c1Xl       = SInt(35.W)
    val c2Xl2      = SInt(29.W)
    val throughout = throughoutGen.cloneType
  }))
  val s2Pipe = s2.handshakePipeIf(true)
 
  s2.valid           := s1Pipe.valid
  s2.bits.op         := s1Pipe.bits.op
  s2.bits.c0         := s1Pipe.bits.c0
  s2.bits.c1Xl       := c1Xl
  s2.bits.c2Xl2      := c2Xl2
  s2.bits.throughout := s1Pipe.bits.throughout
  s1Pipe.ready       := s2.ready
 
  // Stage 3: Align And Sum
  val shift1 = Function.getShift1(s2Pipe.bits.op)
  val shift2 = Function.getShift2(s2Pipe.bits.op)
 
  val aligned1 = (s2Pipe.bits.c1Xl  >> shift1).asSInt
  val aligned2 = (s2Pipe.bits.c2Xl2 >> shift2).asSInt
 
  val result = (s2Pipe.bits.c0 + aligned1 + aligned2)(25, 0)
 
  val s3     = Wire(Decoupled(new OutBundle))
  val s3Pipe = s3.handshakePipeIf(true)
 
  s3.valid           := s2Pipe.valid
  s2Pipe.ready       := s3.ready
  s3.bits.result     := result
  s3.bits.throughout := s2Pipe.bits.throughout
 
  io.out <> s3Pipe
}

class Compose[T <: Bundle](throughoutGen: => T) extends Module {
  class InBundle extends Bundle {
    val exp        = SInt(8.W)
    val polyResult = UInt(26.W)  // x xxxxxxxxxxxxxxxxxxxxxxxxxx the first x is always 1 except log2
    val bypass     = Bool()
    val bypassVal  = UInt(32.W)
    val op         = UInt(3.W)
    val throughout = throughoutGen.cloneType
  }
 
  class OutBundle extends Bundle {
    val result     = UInt(32.W)
    val throughout = throughoutGen.cloneType
  }
 
  val io = IO(new Bundle {
    val in  = Flipped(Decoupled(new InBundle))
    val out = Decoupled(new OutBundle)
  })

  val exp        = io.in.bits.exp
  val polyResult = io.in.bits.polyResult

  // log2 special compose
  val sign    = exp(7)
  val sum     = Cat(exp.asUInt, polyResult)
  val sumAbs  = Mux(sign, (~sum + 1.U(34.W)), sum)
  val lzd     = PriorityEncoder(Reverse(sumAbs))

  val signLog2 = sign
  val expLog2  = (134.U(8.W) - lzd)(7, 0)
  val mantLog2 = (sumAbs << lzd)(32, 10)

  val expExp2  = (exp + 127.S).asUInt(7, 0)
  val mantExp2 = polyResult(24, 2)

  val expRcp   = (126.S - exp).asUInt(7, 0)
  val mantRcp  = polyResult(24, 2)

  val expSqrt  = (127.S + (exp >> 1)).asUInt(7, 0)
  val mantSqrt = polyResult(24, 2)

  val expRsqrt  = (126.S - (exp >> 1)).asUInt(7, 0)
  val mantRsqrt = polyResult(24, 2)

  val result = MuxLookup(io.in.bits.op, 0.U(32.W)) (Seq(
    SFUOp.EXP2  -> Cat(0.U(1.W), expExp2, mantExp2),
    SFUOp.LOG2  -> Cat(signLog2.asUInt, expLog2, mantLog2),
    SFUOp.RCP   -> Cat(0.U(1.W), expRcp, mantRcp),
    SFUOp.SQRT  -> Cat(0.U(1.W), expSqrt, mantSqrt),
    SFUOp.RSQRT -> Cat(0.U(1.W), expRsqrt, mantRsqrt)
  ))

  val s1     = Wire(Decoupled(new OutBundle))
  val s1Pipe = s1.handshakePipeIf(true)
  io.in.ready        := s1.ready
  s1.valid           := io.in.valid
  s1.bits.result     := Mux(io.in.bits.bypass, io.in.bits.bypassVal, result)
  s1.bits.throughout := io.in.bits.throughout

  io.out <> s1Pipe
}

class SFU extends Module {
  class InBundle extends Bundle {
    val x  = UInt(32.W)
    val op = UInt(3.W)
  }
  class OutBundle extends Bundle {
    val result = UInt(32.W)
  }
  val io = IO(new Bundle {
    val in  = Flipped(Decoupled(new InBundle))
    val out = Decoupled(new OutBundle)
  })
  
  // Stage 0: Filter
  class S0Bundle extends Bundle {
    val op = UInt(3.W)
  }
  
  val filter = Module(new Filter[S0Bundle](new S0Bundle))
  io.in.ready                     := filter.io.in.ready
  filter.io.in.valid              := io.in.valid
  filter.io.in.bits.x             := io.in.bits.x
  filter.io.in.bits.op            := io.in.bits.op
  filter.io.in.bits.throughout.op := io.in.bits.op
 
  // Stage 1: RangeReduce
  class S1Bundle extends Bundle {
    val op        = UInt(3.W)
    val bypass    = Bool()
    val bypassVal = UInt(32.W)
  }
 
  val rangeReduce = Module(new RangeReduce[S1Bundle](new S1Bundle))
  filter.io.out.ready                         := rangeReduce.io.in.ready
  rangeReduce.io.in.valid                     := filter.io.out.valid
  rangeReduce.io.in.bits.sign                 := filter.io.out.bits.sign
  rangeReduce.io.in.bits.exponent             := filter.io.out.bits.exponent
  rangeReduce.io.in.bits.mantissa             := filter.io.out.bits.mantissa
  rangeReduce.io.in.bits.op                   := filter.io.out.bits.throughout.op
  rangeReduce.io.in.bits.throughout.op        := filter.io.out.bits.throughout.op
  rangeReduce.io.in.bits.throughout.bypass    := filter.io.out.bits.bypass
  rangeReduce.io.in.bits.throughout.bypassVal := filter.io.out.bits.bypassVal
 
  // Stage 2: LUT
  class S2Bundle extends Bundle {
    val op        = UInt(3.W)
    val xl        = UInt(17.W)
    val exp       = SInt(8.W)
    val bypass    = Bool()
    val bypassVal = UInt(32.W)
  }
 
  val lut = Module(new LookupTable[S2Bundle](new S2Bundle))
  rangeReduce.io.out.ready            := lut.io.in.ready
  lut.io.in.valid                     := rangeReduce.io.out.valid
  lut.io.in.bits.op                   := rangeReduce.io.out.bits.throughout.op
  lut.io.in.bits.index                := rangeReduce.io.out.bits.index
  lut.io.in.bits.throughout.op        := rangeReduce.io.out.bits.throughout.op
  lut.io.in.bits.throughout.xl        := rangeReduce.io.out.bits.xl
  lut.io.in.bits.throughout.exp       := rangeReduce.io.out.bits.exp
  lut.io.in.bits.throughout.bypass    := rangeReduce.io.out.bits.throughout.bypass
  lut.io.in.bits.throughout.bypassVal := rangeReduce.io.out.bits.throughout.bypassVal
 
  // Stage 3-5: Poly (3 cycles)
  class S3Bundle extends Bundle {
    val op        = UInt(3.W)
    val exp       = SInt(8.W)
    val bypass    = Bool()
    val bypassVal = UInt(32.W)
  }
 
  val poly = Module(new Poly[S3Bundle](new S3Bundle))
  lut.io.out.ready                     := poly.io.in.ready
  poly.io.in.valid                     := lut.io.out.valid
  poly.io.in.bits.c0                   := lut.io.out.bits.c0
  poly.io.in.bits.c1                   := lut.io.out.bits.c1
  poly.io.in.bits.c2                   := lut.io.out.bits.c2
  poly.io.in.bits.xl                   := lut.io.out.bits.throughout.xl
  poly.io.in.bits.op                   := lut.io.out.bits.throughout.op
  poly.io.in.bits.throughout.op        := lut.io.out.bits.throughout.op
  poly.io.in.bits.throughout.exp       := lut.io.out.bits.throughout.exp
  poly.io.in.bits.throughout.bypass    := lut.io.out.bits.throughout.bypass
  poly.io.in.bits.throughout.bypassVal := lut.io.out.bits.throughout.bypassVal
 
  // Stage 6: Compose
  val compose = Module(new Compose[Bundle](new Bundle {}))
  poly.io.out.ready             := compose.io.in.ready
  compose.io.in.valid           := poly.io.out.valid
  compose.io.in.bits.exp        := poly.io.out.bits.throughout.exp
  compose.io.in.bits.polyResult := poly.io.out.bits.result
  compose.io.in.bits.bypass     := poly.io.out.bits.throughout.bypass
  compose.io.in.bits.bypassVal  := poly.io.out.bits.throughout.bypassVal
  compose.io.in.bits.op         := poly.io.out.bits.throughout.op
 
  compose.io.out.ready := io.out.ready
  io.out.valid         := compose.io.out.valid
  io.out.bits.result   := compose.io.out.bits.result
}

object SFUGen extends App {
  ChiselStage.emitSystemVerilogFile(
    new SFU,
    Array("--target-dir", "generated"),
    Array("-lowering-options=disallowLocalVariables")
  )
}
