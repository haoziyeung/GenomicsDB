package com.intel.genomicsdb;

import org.testng.annotations.Test;

import java.util.ArrayList;
import java.util.List;

public class GenomicsDBImportConfigurationSpec {

  private static final String ARRAY_FOR_PARTITION0 = "array0";
  private static final String ARRAY_FOR_PARTITION1 = "array1";
  private final String TILEDB_WORKSPACE = "/path/to/junk/folder";

  @Test(groups = {"configuration tests"})
  public void testImportConfiguration() {
    List<GenomicsDBImportConfiguration.Partition> partitions = new ArrayList<>(2);

    GenomicsDBImportConfiguration.TileDBConfig.Builder tB0 =
      GenomicsDBImportConfiguration.TileDBConfig.newBuilder();
    GenomicsDBImportConfiguration.TileDBConfig tileDBConfig_part0 =
      tB0
        .setTiledbWorkspace(TILEDB_WORKSPACE)
        .setTiledbArrayName(ARRAY_FOR_PARTITION0)
        .build();
    GenomicsDBImportConfiguration.TileDBConfig.Builder tB1 =
      GenomicsDBImportConfiguration.TileDBConfig.newBuilder();
    GenomicsDBImportConfiguration.TileDBConfig tileDBConfig_part1 =
      tB1
        .setTiledbWorkspace(TILEDB_WORKSPACE)
        .setTiledbArrayName(ARRAY_FOR_PARTITION1)
        .build();
    GenomicsDBImportConfiguration.Partition.Builder partition0 =
      GenomicsDBImportConfiguration.Partition.newBuilder();
    GenomicsDBImportConfiguration.Partition p0 =
      partition0
        .setBegin(0)
        .setVcfFileName("junk0")
        .setTiledbConfig(tileDBConfig_part0)
        .build();
    GenomicsDBImportConfiguration.Partition.Builder partition1 =
      GenomicsDBImportConfiguration.Partition.newBuilder();
    GenomicsDBImportConfiguration.Partition p1 =
      partition1
        .setBegin(1000000)
        .setVcfFileName("junk1")
        .setTiledbConfig(tileDBConfig_part1)
        .build();

    partitions.add(p0);
    partitions.add(p1);

    GenomicsDBImportConfiguration.ImportConfiguration.Builder configBuilder =
      GenomicsDBImportConfiguration.ImportConfiguration.newBuilder();
    GenomicsDBImportConfiguration.ImportConfiguration importConfiguration =
      configBuilder
        .setDeleteAndCreateTiledbArray(true)
        .setDoPingPongBuffering(true)
        .setProduceTiledbArray(true)
        .setNumParallelVcfFiles(1)
        .setSizePerColumnPartition(10000)
        .setRowBasedPartitioning(false)
        .addAllColumnPartitions(partitions)
        .build();

    assert importConfiguration.isInitialized();

    // Assert has methods
    assert !importConfiguration.hasCallsetMappingFile();
    assert importConfiguration.hasDoPingPongBuffering();
    assert importConfiguration.hasDeleteAndCreateTiledbArray();
    assert !importConfiguration.hasDiscardVcfIndex();
    assert importConfiguration.hasNumParallelVcfFiles();
    assert !importConfiguration.hasOffloadVcfOutputProcessing();
    assert !importConfiguration.hasProduceCombinedVcf();
    assert importConfiguration.hasProduceTiledbArray();
    assert importConfiguration.hasSizePerColumnPartition();

    // Assert gets
    assert importConfiguration.getDoPingPongBuffering();
    assert importConfiguration.getDoPingPongBuffering();
    assert importConfiguration.getDeleteAndCreateTiledbArray();
    assert importConfiguration.getNumParallelVcfFiles() == 1;
    assert importConfiguration.getProduceTiledbArray();
    assert importConfiguration.getSizePerColumnPartition() == 10000;
    assert importConfiguration.getColumnPartitionsCount() == 2;
    assert importConfiguration.getColumnPartitions(0) == p0;
    assert importConfiguration.getColumnPartitions(1) == p1;
  }
}
