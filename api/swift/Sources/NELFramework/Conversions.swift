import CNELFramework
import TensorFlow

internal extension Position {
  /// Creates a new position instance on the Swift API side, corresponding to an exising position
  /// instance on the C API side.
  @inlinable
  init(fromC value: CNELFramework.Position) {
    self.init(x: value.x, y: value.y)
  }

  /// Creates a new position instance on the C API side, corresponding to this position.
  @inlinable
  func toC() -> CNELFramework.Position {
    CNELFramework.Position(x: x, y: y)
  }
}

internal extension Direction {
  @inlinable
  init(fromC value: CNELFramework.Direction) {
    self.init(rawValue: value.rawValue)!
  }

  @inlinable
  func toC() -> CNELFramework.Direction {
    CNELFramework.Direction(rawValue: rawValue)
  }
}

internal extension TurnDirection {
  @inlinable
  init(fromC value: CNELFramework.TurnDirection) {
    self.init(rawValue: value.rawValue)!
  }

  @inlinable
  func toC() -> CNELFramework.TurnDirection {
    CNELFramework.TurnDirection(rawValue: rawValue)
  }
}

internal extension MoveConflictPolicy {
  @inlinable
  init(fromC value: CNELFramework.MovementConflictPolicy) {
    self.init(rawValue: value.rawValue)!
  }

  @inlinable
  func toC() -> CNELFramework.MovementConflictPolicy {
    CNELFramework.MovementConflictPolicy(rawValue: rawValue)
  }
}

internal extension ActionPolicy {
  @inlinable
  init(fromC value: CNELFramework.ActionPolicy) {
    self.init(rawValue: value.rawValue)!
  }

  @inlinable
  func toC() -> CNELFramework.ActionPolicy {
    CNELFramework.ActionPolicy(rawValue: rawValue)
  }
}

internal extension Simulator.Configuration {
  @inlinable
  init(fromC value: SimulatorConfig) {
    self.randomSeed = value.randomSeed
    self.maxStepsPerMove = value.maxStepsPerMove
    self.scentDimSize = value.scentDimSize
    self.colorDimSize = value.colorDimSize
    self.visionRange = value.visionRange
    self.allowedMoves = [
      .up: ActionPolicy(fromC: value.allowedMoveDirections.0),
      .down: ActionPolicy(fromC: value.allowedMoveDirections.1),
      .left: ActionPolicy(fromC: value.allowedMoveDirections.2),
      .right: ActionPolicy(fromC: value.allowedMoveDirections.3)]
    self.allowedTurns = [
      .front: ActionPolicy(fromC: value.allowedRotations.0),
      .back: ActionPolicy(fromC: value.allowedRotations.1),
      .left: ActionPolicy(fromC: value.allowedRotations.2),
      .right: ActionPolicy(fromC: value.allowedRotations.3)]
    self.noOpAllowed = value.noOpAllowed
    self.patchSize = value.patchSize
    self.mcmcIterations = value.mcmcIterations
    self.items = UnsafeBufferPointer(start: value.itemTypes!, count: Int(value.numItemTypes)).map {
      Item(
        fromC: $0,
        scentDimSize: value.scentDimSize,
        colorDimSize: value.colorDimSize,
        itemCount: Int(value.numItemTypes))
    }
    self.agentColor = ShapedArray(
      shape: [Int(colorDimSize)],
      scalars: UnsafeBufferPointer(start: value.agentColor!, count: Int(colorDimSize)))
    self.moveConflictPolicy = MoveConflictPolicy(fromC: value.movementConflictPolicy)
    self.scentDecay = value.scentDecay
    self.scentDiffusion = value.scentDiffusion
    self.removedItemLifetime = value.removedItemLifetime
  }

  @inlinable
  func toC() -> (configuration: SimulatorConfig, deallocate: () -> Void) {
    let (items, itemDeallocators) = self.items
      .map { $0.toC(in: self) }
      .reduce(into: ([ItemProperties](), [() -> Void]())) {
        $0.0.append($1.item)
        $0.1.append($1.deallocate)
      }
    let color = agentColor.scalars
    let cItems = UnsafeMutablePointer<ItemProperties>.allocate(
      capacity: items.count)
    let cColor = UnsafeMutablePointer<Float>.allocate(capacity: color.count)
    cItems.initialize(from: items, count: items.count)
    cColor.initialize(from: color, count: color.count)
    return (
      configuration: SimulatorConfig(
        randomSeed: randomSeed,
        maxStepsPerMove: maxStepsPerMove,
        scentDimSize: scentDimSize,
        colorDimSize: colorDimSize,
        visionRange: visionRange,
        allowedMoveDirections: (
          allowedMoves[.up]?.toC() ?? ActionPolicy.disallowed.toC(),
          allowedMoves[.down]?.toC() ?? ActionPolicy.disallowed.toC(),
          allowedMoves[.left]?.toC() ?? ActionPolicy.disallowed.toC(),
          allowedMoves[.right]?.toC() ?? ActionPolicy.disallowed.toC()),
        allowedRotations: (
          allowedTurns[.front]?.toC() ?? ActionPolicy.disallowed.toC(),
          allowedTurns[.back]?.toC() ?? ActionPolicy.disallowed.toC(),
          allowedTurns[.left]?.toC() ?? ActionPolicy.disallowed.toC(),
          allowedTurns[.right]?.toC() ?? ActionPolicy.disallowed.toC()),
        noOpAllowed: noOpAllowed,
        patchSize: patchSize,
        mcmcIterations: mcmcIterations,
        itemTypes: cItems,
        numItemTypes: UInt32(items.count),
        agentColor: cColor,
        movementConflictPolicy: moveConflictPolicy.toC(),
        scentDecay: scentDecay,
        scentDiffusion: scentDiffusion,
        removedItemLifetime: removedItemLifetime),
      deallocate: { () in
        cItems.deallocate()
        cColor.deallocate()
        for deallocate in itemDeallocators {
          deallocate()
        }
      })
  }
}

internal extension AgentState {
  /// Creates a new agent state on the Swift API side, corresponding to an exising agent state on
  /// the C API side.
  @inlinable
  init(fromC value: AgentSimulationState, for simulator: Simulator) {
    self.position = Position(fromC: value.position)
    self.direction = Direction(fromC: value.direction)

    // Construct the scent vector.
    let scentShape = [Int(simulator.configuration.scentDimSize)]
    let scentBuffer = UnsafeBufferPointer(start: value.scent!, count: scentShape[0])
    self.scent = Tensor(shape: TensorShape(scentShape), scalars: [Float](scentBuffer))

    // Construct the visual field.
    let visionShape = [
      2 * Int(simulator.configuration.visionRange) + 1,
      2 * Int(simulator.configuration.visionRange) + 1,
      Int(simulator.configuration.colorDimSize)]
    let visionSize = Int(
      (2 * simulator.configuration.visionRange + 1) *
      (2 * simulator.configuration.visionRange + 1) *
      simulator.configuration.colorDimSize)
    let visionBuffer = UnsafeBufferPointer(start: value.vision!, count: visionSize)
    self.vision = Tensor(shape: TensorShape(visionShape), scalars: [Float](visionBuffer))

    // Construcct the collected items dictionary.
    let simulatorItems = simulator.configuration.items
    self.items = [Item: Int](uniqueKeysWithValues: zip(
      simulatorItems,
      UnsafeBufferPointer(
        start: value.collectedItems!,
        count: simulatorItems.count).map(Int.init)))
  }
}

internal extension Item {
  @inlinable
  init(fromC value: ItemProperties, scentDimSize: UInt32, colorDimSize: UInt32, itemCount: Int) {
    self.name = String(cString: value.name)
    self.scent = ShapedArray(
      shape: [Int(scentDimSize)],
      scalars: UnsafeBufferPointer(start: value.scent!, count: Int(scentDimSize)))
    self.color = ShapedArray(
      shape: [Int(colorDimSize)],
      scalars: UnsafeBufferPointer(start: value.color!, count: Int(colorDimSize)))
    self.requiredItemCounts = [Int: UInt32](uniqueKeysWithValues: zip(
      0..<itemCount,
      UnsafeBufferPointer(start: value.requiredItemCounts!, count: Int(itemCount))))
    self.requiredItemCosts = [Int: UInt32](uniqueKeysWithValues: zip(
      0..<itemCount,
      UnsafeBufferPointer(start: value.requiredItemCosts!, count: Int(itemCount))))
    self.blocksMovement = value.blocksMovement
    self.energyFunctions = EnergyFunctions(fromC: value.energyFunctions)
  }

  @inlinable
  func toC(
    in configuration: Simulator.Configuration
  ) -> (item: ItemProperties, deallocate: () -> Void) {
    let scent = self.scent.scalars
    let color = self.color.scalars
    let counts = configuration.items.indices.map { requiredItemCounts[$0, default: 0] }
    let costs = configuration.items.indices.map { requiredItemCosts[$0, default: 0] }

    // Allocate C arrays and copy data.
    let cScent = UnsafeMutablePointer<Float>.allocate(capacity: scent.count)
    let cColor = UnsafeMutablePointer<Float>.allocate(capacity: color.count)
    let cCounts = UnsafeMutablePointer<UInt32>.allocate(capacity: counts.count)
    let cCosts = UnsafeMutablePointer<UInt32>.allocate(capacity: costs.count)
    cScent.initialize(from: scent, count: scent.count)
    cColor.initialize(from: color, count: color.count)
    cCounts.initialize(from: counts, count: counts.count)
    cCosts.initialize(from: costs, count: costs.count)

    let cEnergyFunctions = energyFunctions.toC()
    return (
      item: ItemProperties(
        name: name,
        scent: cScent,
        color: cColor,
        requiredItemCounts: cCounts, 
        requiredItemCosts: cCosts, 
        blocksMovement: blocksMovement,
        energyFunctions: cEnergyFunctions.energyFunctions),
      deallocate: { () in 
        cScent.deallocate()
        cColor.deallocate()
        cCounts.deallocate()
        cCosts.deallocate()
        cEnergyFunctions.deallocate()
      })
  }
}

internal extension EnergyFunctions {
  @inlinable
  init(fromC value: CNELFramework.EnergyFunctions) {
    self.intensityFn = IntensityFunction(fromC: value.intensityFn)
    self.interactionFns = UnsafeBufferPointer(
      start: value.interactionFns!,
      count: Int(value.numInteractionFns)
    ).map { InteractionFunction(fromC: $0) }
  }

  @inlinable
  func toC() -> (energyFunctions: CNELFramework.EnergyFunctions, deallocate: () -> Void) {
    let cIntensityFn = intensityFn.toC()
    let (interactionFns, interactionFnDeallocators) = self.interactionFns
      .map { $0.toC() }
      .reduce(into: ([CNELFramework.InteractionFunction](), [() -> Void]())) {
        $0.0.append($1.interactionFunction)
        $0.1.append($1.deallocate)
      }
    let cInteractionFns = UnsafeMutablePointer<CNELFramework.InteractionFunction>.allocate(
      capacity: interactionFns.count)
    cInteractionFns.initialize(from: interactionFns, count: interactionFns.count)
    return (
      energyFunctions: CNELFramework.EnergyFunctions(
        intensityFn: cIntensityFn.intensityFunction,
        interactionFns: cInteractionFns,
        numInteractionFns: UInt32(interactionFns.count)),
      deallocate: { () in 
        cIntensityFn.deallocate()
        cInteractionFns.deallocate()
        for deallocate in interactionFnDeallocators {
          deallocate()
        }
      })
  }
}

internal extension IntensityFunction {
  @inlinable
  init(fromC value: CNELFramework.IntensityFunction) {
    self.id = value.id
    self.arguments = [Float](UnsafeBufferPointer(start: value.args!, count: Int(value.numArgs)))
  }

  @inlinable
  func toC() -> (intensityFunction: CNELFramework.IntensityFunction, deallocate: () -> Void) {
    let cArgs = UnsafeMutablePointer<Float>.allocate(capacity: arguments.count)
    cArgs.initialize(from: arguments, count: arguments.count)
    return (
      intensityFunction: CNELFramework.IntensityFunction(
        id: id,
        args: cArgs,
        numArgs: UInt32(arguments.count)),
      deallocate: { () in cArgs.deallocate() })
  }
}

internal extension InteractionFunction {
  @inlinable
  init(fromC value: CNELFramework.InteractionFunction) {
    self.id = value.id
    self.itemId = value.itemId
    self.arguments = [Float](UnsafeBufferPointer(start: value.args!, count: Int(value.numArgs)))
  }

  @inlinable
  func toC() -> (interactionFunction: CNELFramework.InteractionFunction, deallocate: () -> Void) {
    let cArgs = UnsafeMutablePointer<Float>.allocate(capacity: arguments.count)
    cArgs.initialize(from: arguments, count: arguments.count)
    return (
      interactionFunction: CNELFramework.InteractionFunction(
        id: id,
        itemId: itemId,
        args: cArgs,
        numArgs: UInt32(arguments.count)),
      deallocate: { () in cArgs.deallocate() })
  }
}
