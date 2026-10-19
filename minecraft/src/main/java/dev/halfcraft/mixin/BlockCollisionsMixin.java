package dev.halfcraft.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import com.llamalad7.mixinextras.sugar.Local;
import dev.halfcraft.world.HostCollision;
import dev.halfcraft.world.HostDig;
import net.minecraft.core.BlockPos;
import net.minecraft.world.level.BlockCollisions;
import net.minecraft.world.level.BlockGetter;
import net.minecraft.world.level.CollisionGetter;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.phys.shapes.CollisionContext;
import net.minecraft.world.phys.shapes.EntityCollisionContext;
import net.minecraft.world.phys.shapes.Shapes;
import net.minecraft.world.phys.shapes.VoxelShape;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

/**
 * Adds Half-Life's geometry and its void beyond the map (outside dug cells: HostDig.collisionShape) to
 * every block-collision query. Vanilla movement, step-up, onGround and fall-damage logic then run
 * unchanged against it. The players colliding with Half-Life's exact triangles (HostCollider) get its
 * void here alone: the void has no triangles.
 */
@Mixin(BlockCollisions.class)
public abstract class BlockCollisionsMixin {
	@WrapOperation(
		method = "computeNext",
		at = @At(
			value = "INVOKE",
			target = "Lnet/minecraft/world/phys/shapes/CollisionContext;getCollisionShape(Lnet/minecraft/world/level/block/state/BlockState;Lnet/minecraft/world/level/CollisionGetter;Lnet/minecraft/core/BlockPos;)Lnet/minecraft/world/phys/shapes/VoxelShape;"
		)
	)
	private VoxelShape halfcraft$addHostShape(
		CollisionContext context, BlockState state, CollisionGetter level, BlockPos pos, Operation<VoxelShape> original, @Local BlockGetter chunk
	) {
		VoxelShape blockShape = original.call(context, state, level, pos);
		boolean triangles = context instanceof EntityCollisionContext entityContext && HostCollision.usesSmoothCollider(entityContext.getEntity());
		VoxelShape host = HostDig.collisionShape(chunk, pos, !triangles);
		if (host == null) {
			return blockShape;
		}
		return blockShape.isEmpty() ? host : Shapes.or(blockShape, host);
	}
}
